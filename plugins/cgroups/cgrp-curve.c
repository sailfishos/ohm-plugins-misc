#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>

#include "cgrp-plugin.h"
#include "mm.h"
#include "list.h"


/*
# response-curve priority [-10, 10] '1 / 3 * ln(x^2)' [-100, 100]
# response-curve out-of-memory [-20, 20] 'x' [-100, 100]
*/




/*
 * dynamically registrable curve functions
 */

typedef struct {
    char         *name;                       /* name of this function */
    list_hook_t   hook;                       /* hook to more functions */
    double      (*fn)(double, void *);        /* actual curve function */
    void         *data;                       /* opaque data */
} curve_func_t;

static list_hook_t curves;                    /* registered curve functions */

int  rspcrv_register(const char *, double (*)(double, void *), void *);
void rspcrv_unregister(const char *);
static curve_func_t *rspcrv_find(const char *);


/*
 * generalized response curves
 */

/*
 * Our 'generalized' response curve is specified using
 *  - a function f (should be monotonically increasing),
 *  - a range [cmin, cmax] to select part of the curve to use
 *  - an allowed input range [imin, imax],
 *  - a desired output range [omin, omax]
 *
 * We then use a straightforward (= dumb) alogrithm to calculate the reponse
 * mapping [imin, imax] -> [omin, omax] in the following way:
 *
 *   1) scale the input ([imin, imax]) to x ([cmin, cmax])
 *   2) calculate y = f(x) and normalize it to [0, 1] (by linear scaling
 *      from [f(cmin), f(cmax)])
 *   3) map y to the output ([omin, omax]) (linear scaling from [0, 1])
 */

typedef struct {
    double (*fn)(double, void *);           /* response curve function */
    void    *data;                          /* opaque data for f */
    double   cmin, cmax;                    /* curve range (x) to use */
    double   imin, imax;                    /* allowed input range */
    double   omin, omax;                    /* desired output range */

    /* some pre-calculated values */
    double   d_c, d_i, d_o;
    double   f_cmin, f_cmax;
    char    *f;
} cgrp_rspcrv_t;


static cgrp_rspcrv_t *rspcrv_create (const char *, double, double,
                                     double, double, double, double);
static double         rspcrv_calc   (cgrp_rspcrv_t *, double);
static void           rspcrv_destroy(cgrp_rspcrv_t *);

static int check_curve(cgrp_rspcrv_t *);


/*
 * symbolically interpreted curve functions
 *
 * We provide two mechanisms to allow (almost) arbitrary response curves
 * for dynamic OOM- and priority-adjustment. The first one is an interface
 * for registering C implementation of a curve function that can then be
 * referred to by name when specifying response curves.
 *
 * The second one provides a way to specify and evaluate a curve function
 * in symbolic form. The code below uses a straightforward implementation
 * of the Shunting-yard algorithm to convert a function from infix to
 * reverse-polish notation. This is used as the internal representation
 * of the curve and can be used to evaluate its value for a given input.
 */

#define RPN_MAX_TOKENS 256

static void   *rpn_parse(const char *);
static void    rpn_free (void *);
static double  rpn_calc (double, void *);



/********************
 * curve_init
 ********************/
int
curve_init(cgrp_context_t *ctx)
{
    (void)ctx;
    
    list_init(&curves);

    return TRUE;
}


/********************
 * curve_exit
 ********************/
void
curve_exit(cgrp_context_t *ctx)
{
    curve_func_t *cfn;
    list_hook_t  *p, *n;

    list_foreach(&curves, p, n) {
        cfn = list_entry(p, curve_func_t, hook);
        rspcrv_unregister(cfn->name);
    }

    curve_destroy(ctx->prio_curve);
    curve_destroy(ctx->oom_curve);

    ctx->prio_curve = NULL;
    ctx->oom_curve  = NULL;
}


/********************
 * curve_create
 ********************/
cgrp_curve_t *
curve_create(const char *fn, double cmin, double cmax,
             int imin, int imax, int omin, int omax)
{
    cgrp_curve_t  *crv;
    cgrp_rspcrv_t *rsp;
    int            n, i;
    
    n   = imax - imin + 1;
    crv = NULL;

    if ((rsp = rspcrv_create(fn, cmin, cmax, 1.0 * imin, 1.0 * imax,
                             1.0 * omin, 1.0 * omax)) == NULL) {
        OHM_ERROR("cgrp: could not create response curve '%s'", fn);
        return NULL;
    }
    
    if (ALLOC_OBJ(crv) != NULL && (crv->out = ALLOC_ARR(int, n)) != NULL) {
        crv->min = imin;
        crv->max = imax;

        crv->out[0] = omin;

        errno = 0;
        for (i = imin + 1; i < imax; i++) {
            crv->out[i-imin] = (int)(rspcrv_calc(rsp, 1.0 * i) + 0.5);

            if (errno != 0) {
                OHM_ERROR("cgrp: evaluation error for '%s'", rsp->f);
                curve_destroy(crv);
                crv = NULL;
                goto out;
            }
        }
        
        crv->out[n-1] = omax;
    }
    else {
        OHM_ERROR("cgrp: failed to allocate curve '%s'", fn);
        FREE(crv);
        crv = NULL;
    }
    
 out:
    rspcrv_destroy(rsp);

    return crv;
}


/********************
 * curve_destroy
 ********************/
void
curve_destroy(cgrp_curve_t *crv)
{
    if (crv != NULL) {
        FREE(crv->out);
        FREE(crv);
    }
}


/********************
 * curve_map
 ********************/
int
curve_map(cgrp_curve_t *crv, int x, int *clamped)
{
    int y;
    
    if (crv == NULL)
        y = x;
    else {
        if      (x < crv->min) x = crv->min;
        else if (x > crv->max) x = crv->max;
        
        y = crv->out[x - crv->min];
    }

    if (clamped != NULL)
        *clamped = x;

    return y;
}


/********************
 * rspcrv_register
 ********************/
int
rspcrv_register(const char *name, double (*fn)(double, void *), void *data)
{
    curve_func_t *cfn;
    
    if (rspcrv_find(name) != NULL) {
        OHM_ERROR("cgrp: curve function '%s' already registered", name);
        return FALSE;
    }

    if (ALLOC_OBJ(cfn) != NULL) {
        list_init(&cfn->hook);
        
        cfn->fn   = fn;
        cfn->data = data;
        cfn->name   = STRDUP(name);

        if (cfn->name == NULL) {
            FREE(cfn);
            cfn = NULL;
        }

        list_append(&curves, &cfn->hook);
        OHM_INFO("cgrp: registered response curve function '%s': %p", name, fn);
    }
    else
        OHM_ERROR("cgrp: failed to allocate curve function '%s'", name);
    
    return (cfn != NULL);
}


/********************
 * rspcrv_unregister
 ********************/
void
rspcrv_unregister(const char *name)
{
    curve_func_t *cfn;
    
    if ((cfn = rspcrv_find(name)) != NULL) {
        list_delete(&cfn->hook);

        FREE(cfn->name);
        FREE(cfn);
        
        OHM_INFO("cgrp: unregistered response curve function '%s'", name);
    }
}


/********************
 * rspcrv_find
 ********************/
static curve_func_t *
rspcrv_find(const char *name)
{
    curve_func_t *cfn;
    list_hook_t  *p, *n;

    list_foreach(&curves, p, n) {
        cfn = list_entry(p, curve_func_t, hook);
        
        if (!strcmp(cfn->name, name))
            return cfn;
    }
    
    return NULL;
}


/********************
 * rspcrv_create
 ********************/
cgrp_rspcrv_t *
rspcrv_create(const char *fn,
              double cmin, double cmax,
              double imin, double imax,
              double omin, double omax)
{
    cgrp_rspcrv_t *crv;
    curve_func_t  *cfn;

    if (ALLOC_OBJ(crv) != NULL) {
        crv->f    = STRDUP(fn);
        crv->cmin = cmin; crv->cmax = cmax;
        crv->imin = imin; crv->imax = imax;
        crv->omin = omin; crv->omax = omax;
        
        if ((cfn = rspcrv_find(crv->f)) != NULL) {
            crv->fn   = cfn->fn;
            crv->data = cfn->data;
        }
        else {
            crv->fn   = rpn_calc;
            crv->data = rpn_parse(crv->f);
            
            if (crv->data == NULL) {
                rspcrv_destroy(crv);
                return NULL;
            }
        }
        
        crv->d_c = cmax - cmin;
        crv->d_i = imax - imin;
        crv->d_o = omax - omin;

        errno = 0;
        crv->f_cmin = crv->fn(cmin, crv->data);
        crv->f_cmax = crv->fn(cmax, crv->data);
        if (errno != 0) {
            OHM_ERROR("cgrp: evaluation error for '%s'", crv->f);
            rspcrv_destroy(crv);
            return NULL;
        }
        
        if (!check_curve(crv)) {
            rspcrv_destroy(crv);
            return NULL;
        }
    }
    
    return crv;
}


/********************
 * rspcrv_destroy
 ********************/
void
rspcrv_destroy(cgrp_rspcrv_t *crv)
{
    if (crv != NULL) {
        FREE(crv->f);
        if (crv->data != NULL && crv->fn == rpn_calc)
            rpn_free(crv->data);
        FREE(crv);
    }
}


/********************
 * rspcrv_calc
 ********************/
double
rspcrv_calc(cgrp_rspcrv_t *crv, double input)
{
    double x, y, output;
    
    if (input < crv->imin)
        input = crv->imin;
    else if (input > crv->imax)
        input = crv->imax;
    
    /* normalize to [0, 1] */
    x = (input - crv->imin) / crv->d_i;
    /* translate to [cmin, cmax] */
    x = crv->cmin + x * crv->d_c;

    OHM_DEBUG(DBG_CURVE, "translated input: %f -> %f", input, x);

    /* calculate normalized [0, 1] output */
    y = (crv->fn(x, crv->data) - crv->f_cmin) / (crv->f_cmax - crv->f_cmin);
    /* translate to [omin, omax] */
    output = crv->omin + crv->d_o * y;
    
    OHM_DEBUG(DBG_CURVE, "calculated output: %f", output);
    
    return output;
}


/********************
 * check_monotonic
 ********************/
static int
check_monotonic(double (*fn)(double, void *), void *data,
                double min, double max, double step)
{
    double x, y, prev;
    int    diff;

    diff = 0;
    prev = fn(min, data);
    for (x = min + step; x <= max; x += step) {
        y = fn(x, data);
        
        if (!diff) {
            if      (y < prev) diff = -1;
            else if (y > prev) diff = +1;
        }

        if ((diff < 0 && y > prev) || (diff > 0 && y < prev))
            return FALSE;

        prev = y;
    }

    return TRUE;
}


/********************
 * check_curve
 ********************/
static int
check_curve(cgrp_rspcrv_t *crv)
{
    errno = 0;

    if (!check_monotonic(crv->fn, crv->data, crv->cmin, crv->cmax,
                         1.0 / (crv->imax - crv->imin))) {
        OHM_ERROR("cgrp: function '%s' is not monotonic!", crv->f);
        return FALSE;
    }
    
    if (errno != 0) {
        OHM_ERROR("cgrp: evaluation error for '%s'", crv->f);
        return FALSE;
    }
    
    return TRUE;
}


/*****************************************************************************
 *                     *** symbolic function evaluation ***                  *
 *****************************************************************************/

extern double log2(double);


/*
 * operator associativity
 */

enum {
    ASSOC_RIGHT = 1,                           /* right associative operator */
    ASSOC_LEFT,                                /* left associative operator */
};


/*
 * token types
 */

typedef enum {
    TOKEN_UNKNOWN = 0,
    TOKEN_VARIABLE,                            /* variable */
    TOKEN_CONSTANT,                            /* constant (double) */
    TOKEN_OPERATOR,                            /* +,-,*,/,^ */
    TOKEN_FUNCTION,                            /* function (ln,log2,log,...) */
    TOKEN_PAREN_OPEN,                          /* left parenthesis */
    TOKEN_PAREN_CLOSE,                         /* right parenthesis */
    TOKEN_END                                  /* end of input */
} token_type_t;


/*
 * operators
 */

typedef enum {
    OPER_UNKNOWN = 0,
    OPER_PLUS,
    OPER_MINUS,
    OPER_MUL,
    OPER_DIV,
    OPER_EXP,
} operator_t;

const char *operator_name[] = {
    "unknown", "+", "-", "*", "/", "^"
};


int operator_assoc[] = {
    [OPER_PLUS]  = ASSOC_LEFT,
    [OPER_MINUS] = ASSOC_LEFT,
    [OPER_MUL]   = ASSOC_LEFT,
    [OPER_DIV]   = ASSOC_LEFT,
    [OPER_EXP]   = ASSOC_RIGHT
};

int operator_prec[] = {
    [OPER_PLUS]  = 2,
    [OPER_MINUS] = 2,
    [OPER_MUL]   = 3,
    [OPER_DIV]   = 3,
    [OPER_EXP]   = 4
};


/*
 * functions
 */

typedef enum {
    FUNC_UNKNOWN = 0,
    FUNC_LN,                                   /* natural logarithm */
    FUNC_LOG2,                                 /* 2-base logarithm */
    FUNC_LOG10,                                /* 10-base logarithm */
    FUNC_SIN,                                  /* sine */
    FUNC_COS,                                  /* cosine */
    FUNC_ABS,                                  /* absolute value */
} function_t;

const char *function_name[] = {
    "unknown", "ln", "log2", "log10", "sin", "cos", "abs"
};


/*
 * a generic token
 */

typedef struct {
    token_type_t type;                         /* token type, TOKEN_* */
    union {
        double     val;                        /* constant value */
        operator_t op;                         /* operator type */
        function_t fn;                         /* function type */
    };
} token_t;


static inline function_t func_lookup(const char *name)
{
    static struct {
        const char *name;
        function_t  fn;
    } functions[] = {
        { "ln"   , FUNC_LN      },
        { "log2" , FUNC_LOG2    },
        { "log10", FUNC_LOG10   },
        { "sin"  , FUNC_SIN     },
        { "cos"  , FUNC_COS     },
        { "abs"  , FUNC_ABS     },
        { NULL   , FUNC_UNKNOWN }
    }, *func;

    for (func = functions; func->name; func++)
        if (!strcmp(func->name, name))
            return func->fn;
    
    return FUNC_UNKNOWN;
}


static const char *get_token(const char *input, token_t *token)
{
    const char *in;
    char       *next, func[64];
    size_t      len;

    for (in = input; *in == ' ' || *in == '\t'; in++)
        ;
    
    switch (*in) {
    case '+':
        if ('0' <= in[1] && in[1] <= '9') {
            errno = 0;
            token->val = strtod(in, &next);
            if (errno != 0 || next == in)
                token->type = TOKEN_UNKNOWN;
            else {
                token->type = TOKEN_CONSTANT;
                in = next;
            }
        }
        else {
            token->type = TOKEN_OPERATOR;
            token->op   = OPER_PLUS;
            in++;
        }
        return in;
        
    case '-':
        if ('0' <= in[1] && in[1] <= '9') {
            errno = 0;
            token->val = strtod(in, &next);
            if (errno != 0 || next == in)
                token->type = TOKEN_UNKNOWN;
            else {
                token->type = TOKEN_CONSTANT;
                in = next;
            }
        }
        else {
            token->type = TOKEN_OPERATOR;
            token->op   = OPER_MINUS;
            in++;
        }
        return in;

    case '*':
        token->type = TOKEN_OPERATOR;
        token->op   = OPER_MUL;
        return in + 1;
        
    case '/':
        token->type = TOKEN_OPERATOR;
        token->op   = OPER_DIV;
        return in + 1;
        
    case '^':
        token->type = TOKEN_OPERATOR;
        token->op   = OPER_EXP;
        return in + 1;
        
    case '(':
        token->type = TOKEN_PAREN_OPEN;
        return in + 1;
        
    case ')':
        token->type = TOKEN_PAREN_CLOSE;
        return in + 1;

    case 'x':
        token->type = TOKEN_VARIABLE;
        return in + 1;
        
    case '0' ... '9':
        errno = 0;
        token->val = strtod(in, &next);
        if (errno != 0 || next == in)
            token->type = TOKEN_UNKNOWN;
        else {
            token->type = TOKEN_CONSTANT;
            in          = next;
        }
        return in;

    case 'l':
    case 's':
    case 'c':
        input = in;
        len   = 0;
        while ((('a' <= *in && *in <= 'z')||('0' <= *in && *in <= '9')) &&
               len < sizeof(func) - 1)
            func[len++] = *in++;
        func[len] = '\0';
        
        token->fn = func_lookup(func);

        if (token->fn == FUNC_UNKNOWN) {
            token->type = TOKEN_UNKNOWN;
            in = input;
        }
        else
            token->type = TOKEN_FUNCTION;
        
        return in;

    case '\0':
        token->type = TOKEN_END;
        return in;

    default:
        token->type = TOKEN_UNKNOWN;
        return in;
    }
}


/********************
 * rpn_parse
 ********************/
static void *
rpn_parse(const char *expr)
{
#define QUEUE(t) do {                                                   \
        if (oi >= RPN_MAX_TOKENS - 1) {                                 \
            OHM_ERROR("cgrp: RPN parser output overflow");              \
            FREE(stack);                                                \
            FREE(output);                                               \
            return NULL;                                                \
        }                                                               \
        output[oi++] = t;                                               \
    } while (0)

#define PUSH(t) do {                                                    \
        if (si >= RPN_MAX_TOKENS - 1) {                                 \
            OHM_ERROR("cgrp: RPN parser stack overflow");               \
            FREE(stack);                                                \
            FREE(output);                                               \
            return NULL;                                                \
        }                                                               \
        stack[si++] = t;                                                \
    } while (0)
    
#define POP() ({                                                       \
        token_t _t;                                                    \
        if (si < 1)                                                    \
            _t.type = TOKEN_UNKNOWN;                                   \
        else {                                                         \
            si--;                                                      \
            _t = stack[si];                                            \
            stack[si].type = TOKEN_UNKNOWN;                            \
        }                                                              \
        _t; })

    const char *input;
    token_t    *stack, *output, t1, t2;
    int         si, oi, assoc1, prec1, prec2;

    input = expr;
    stack = output = NULL;

    stack  = ALLOC_ARR(token_t, RPN_MAX_TOKENS);
    output = ALLOC_ARR(token_t, RPN_MAX_TOKENS);
    
    if (stack == NULL || output == NULL) {
        OHM_ERROR("cgrp: failed to allocate RPN tokens");
        goto error;
    }

    si = oi = 0;
    while (si < RPN_MAX_TOKENS && oi < RPN_MAX_TOKENS) {
        input = get_token(input, &t1);
        
        switch (t1.type) {
        case TOKEN_CONSTANT:
        case TOKEN_VARIABLE:
            QUEUE(t1);
            break;
            
        case TOKEN_FUNCTION:
            PUSH(t1);
            break;

        case TOKEN_OPERATOR:
            while ((t2 = POP()).type == TOKEN_OPERATOR) {
                assoc1 = operator_assoc[t1.op];
                prec1  = operator_prec[t1.op];
                prec2  = operator_prec[t2.op];
                
                if ((assoc1 == ASSOC_LEFT  && prec1 <= prec2) ||
                    (assoc1 == ASSOC_RIGHT && prec1 <  prec2))
                    QUEUE(t2);
                else {
                    PUSH(t2);                   /* push back */
                    break;
                }
            }
            if (t2.type != TOKEN_UNKNOWN && t2.type != TOKEN_OPERATOR)
                PUSH(t2);                       /* push non-operator back */
            PUSH(t1);
            break;

        case TOKEN_PAREN_OPEN:
            PUSH(t1);
            break;

        case TOKEN_PAREN_CLOSE:
            while ((t2 = POP()).type != TOKEN_PAREN_OPEN) {
                if (t2.type == TOKEN_OPERATOR)
                    QUEUE(t2);
                else {
                    if (t2.type == TOKEN_END)
                        OHM_ERROR("cgrp: mismatched parenthesis in '%s'", expr);
                    else
                        OHM_ERROR("cgrp: failed to parse '%s'", expr);
                    goto error;
                }
            }
            
            if ((t2 = POP()).type != TOKEN_FUNCTION)
                PUSH(t2);
            else {
                QUEUE(t2);
            }
            break;
            
        case TOKEN_END:
            while ((t2 = POP()).type != TOKEN_UNKNOWN) {
                if (t2.type == TOKEN_OPERATOR)
                    QUEUE(t2);
                else if (t2.type == TOKEN_PAREN_OPEN) {
                    OHM_ERROR("cgrp: mismatched parenthesis in '%s'", expr);
                    goto error;
                }
                else {
                    OHM_ERROR("cgrp: failed to parse '%s'", expr);
                    goto error;
                }
            }
            t2.type = TOKEN_END;
            QUEUE(t2);
            FREE(stack);
            return output;

        case TOKEN_UNKNOWN:
            OHM_ERROR("cgrp: failed to parse '%s'", expr);
            goto error;
        }
    }
    
 error:
    FREE(stack);
    FREE(output);
    return NULL;

#undef QUEUE
#undef PUSH
#undef POP
}


/********************
 * rpn_free
 ********************/
static void
rpn_free(void *tokens)
{
    FREE(tokens);
}


/********************
 * rpn_calc
 ********************/
static double
rpn_calc(double x, void *data)
{
#undef ABS
#define ABS(v) ((v) >= 0 ? (v) : -(v))
#define PUSH(t) do {                                                    \
        if (si >= RPN_MAX_TOKENS - 1) {                                 \
            OHM_ERROR("cgrp: RPN evaluator stack overflow");            \
            return 0.0;                                                 \
        }                                                               \
        stack[si++] = t;                                                \
    } while (0)
    
#define POP() ({                                                       \
        token_t _t;                                                    \
        if (si < 1)                                                    \
            _t.type = TOKEN_UNKNOWN;                                   \
        else {                                                         \
            si--;                                                      \
            _t = stack[si];                                            \
            stack[si].type = TOKEN_UNKNOWN;                            \
        }                                                              \
        _t; })


    token_t *rpn, stack[RPN_MAX_TOKENS], *t, v, arg1, arg2;
    int      si;

    memset(stack, 0, sizeof(stack));
    si  = 0;
    rpn = (token_t *)data;

    for (t = rpn; t->type != TOKEN_END; t++) {
        switch (t->type) {
        case TOKEN_CONSTANT:
            PUSH(*t);
            break;

        case TOKEN_VARIABLE:
            v.type = TOKEN_CONSTANT;
            v.val  = x;
            PUSH(v);
            break;

        case TOKEN_OPERATOR:
            arg1 = POP();
            arg2 = POP();
            
            if (arg1.type != TOKEN_CONSTANT || arg2.type != TOKEN_CONSTANT) {
                OHM_ERROR("cgrp: RPN evaluation: invalid operator arguments");
                return 0.0;
            }

            v.type = TOKEN_CONSTANT;
            switch (t->op) {
            case OPER_PLUS:  v.val = arg2.val + arg1.val;     break;
            case OPER_MINUS: v.val = arg2.val - arg1.val;     break;
            case OPER_MUL:   v.val = arg2.val * arg1.val;     break;
            case OPER_DIV:   v.val = arg2.val / arg1.val;     break;
            case OPER_EXP:   v.val = pow(arg2.val, arg1.val); break;
            default:
                OHM_ERROR("cgrp: RPN evaluation: unknown operator");
                return 0.0;
            }
            PUSH(v);
            break;

        case TOKEN_FUNCTION:
            v = POP();
            
            if (v.type != TOKEN_CONSTANT) {
                OHM_ERROR("cgrp: RPN evaluation: invalid function argument");
                return 0.0;
            }
            
            switch (t->fn) {
            case FUNC_LN:    v.val = log(v.val);   break;
            case FUNC_LOG2:  v.val = log2(v.val);  break;
            case FUNC_LOG10: v.val = log10(v.val); break;
            case FUNC_SIN:   v.val = sin(v.val);   break;
            case FUNC_COS:   v.val = cos(v.val);   break;
            case FUNC_ABS:   v.val = ABS(v.val);   break;
            default:
                OHM_ERROR("cgrp: RPN evaluation: unknown function");
                return 0.0;
            }

            PUSH(v);
            break;
            
        default:
            OHM_ERROR("cgrp: RPN evaluation: unknown function");
        }
    }

    if (si != 1 || (v = POP()).type != TOKEN_CONSTANT) {
        OHM_ERROR("cgrp: RPN evaluation: invalid rpn expression");
        return 0.0;
    }
    else
        return v.val;

#undef PUSH
#undef POP
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

