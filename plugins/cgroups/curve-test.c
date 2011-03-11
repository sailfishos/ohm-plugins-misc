/*
 *  gcc -Wall `pkg-config --cflags dbus-1`   \
 *            `pkg-config --cflags glib-2.0` \
 *      curve-test.c -o curve-test -lm
 */

#include <stdarg.h>

#define OHM_INFO(fmt, args...)    printf("I: "fmt"\n" , ## args)
#define OHM_WARNING(fmt, args...) printf("W: "fmt"\n" , ## args)
#define OHM_ERROR(fmt, args...)   printf("E: "fmt"\n" , ## args)

#define OHM_DEBUG(flag, fmt, args...) do {      \
        if (flag)                               \
            printf("D: "fmt"\n" , ## args);     \
    } while (0)

#undef FALSE
#undef TRUE
#define FALSE 0
#define TRUE (!FALSE)

static int DBG_CURVE;


#if 0

typedef struct {
    int  min;                                 /* input limit low */
    int  max;                                 /*   and high limits */
    int *out;                                 /* output values */
} cgrp_curve_t;

typedef struct {
    cgrp_curve_t *prio_curve;
    cgrp_curve_t *oom_curve;
} cgrp_context_t;

#endif

#include "cgrp-curve.c"


static int log_level;

void ohm_log(OhmLogLevel level, const gchar *format, ...)
{
    va_list ap;
    
    if (log_level & level) {
        va_start(ap, format);
        vfprintf(stdout, format, ap);
        va_end(ap);
    }
}


int __trace_printf(int id, const char *file, int line, const char *func,
                   const char *format, ...)
{
    va_list ap;

    (void)file;
    (void)line;
    (void)func;

    if (!id)
        return FALSE;
    
    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);

    return TRUE;
}


/*****************************************************************************
 *             *** symbolic evaluation and curve mapping test ***            *
 *****************************************************************************/

#include <getopt.h>

#define fatal(fmt, args...) do {                                \
        fprintf(stderr, "fatal error: "fmt"\n" , ## args);      \
        exit(1);                                                \
    } while (0)


static void __attribute__((unused)) print_token(token_t *token, const char *input)
{
    switch (token->type) {
    case TOKEN_UNKNOWN:
        printf("unknown token at '%s'\n", input);
        break;

    case TOKEN_VARIABLE:
        printf("x\n");
        break;

    case TOKEN_CONSTANT:
        printf("constant %f\n", token->val);
        break;

    case TOKEN_OPERATOR:
        printf("operator %s\n", operator_name[token->op]);
        break;

    case TOKEN_FUNCTION:
        printf("function %s\n", function_name[token->fn]);
        break;

    case TOKEN_PAREN_OPEN:
        printf("(\n");
        break;

    case TOKEN_PAREN_CLOSE:
        printf(")\n");
        break;

    case TOKEN_END:
        printf("end\n");
        break;
        
    default:
        printf("???\n");
    }
}


#define SVG_HEADER \
    "<?xml version=\"1.0\" standalone=\"no\"?>\n"             \
    "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\"\n"      \
    "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n" \
    "\n"                                                      \
    "<svg width=\"100%\" height=\"100%\" version=\"1.1\"\n"   \
    "xmlns=\"http://www.w3.org/2000/svg\">\n"                 \
    "\n"

#define SVG_FOOTER "</svg>\n"

    /*
      <polyline points=\"0,0 0,20 20,20 20,40 40,40 40,60\"
      style=\"fill:white;stroke:red;stroke-width:2\"/>
    */





int curve_to_svg(cgrp_curve_t *crv, const char *path, int imin, int imax)
{
#define SVG_WIDTH  600
#define SVG_HEIGHT 600

    FILE   *svg;
    char   *t;
    int     x, y, clamped;
    int     omin, omax;
    int     svdx, svdy;
    double  svfx, svfy, svx, svy;

    if ((svg = fopen(path, "w")) == NULL)
        fatal("failed to open file '%s'", path);

    omin = curve_map(crv, imin, &imin);
    omax = curve_map(crv, imax, &imax);

    svfx = (1.0 * SVG_WIDTH)  / (imax - imin);
    svfy = (1.0 * SVG_HEIGHT) / (omax - omin);
    svdx = 0 - imin;
    svdy = 0;

    fprintf(svg, "%s", SVG_HEADER);
    
    fprintf(svg, "<polyline points=\"");
    for (x = imin, t = ""; x <= imax; x++, t = " ") {
        y = curve_map(crv, x, &clamped);
        
        svx = svfx * (x + svdx);
        svy = svfy * y;
        if (svdy == 0)
            svdy = -svy;
        printf("svg: (%d, %d) -> (%f, %f)\n", x, y, svx, svy + svdy);
        fprintf(svg, "%s%f,%f", t, svx, svy + svdy);
        fprintf(svg, " %f,%f", svx+1, svy + svdy);
    }
    fprintf(svg, "\"");
    fprintf(svg, "\n%s\n",
            "style=\"fill:white;stroke:black;stroke-width:2\"/>");
    
    fprintf(svg, "%s", SVG_FOOTER);
    fclose(svg);

    return 0;
}


int main(int argc, char *argv[])
{
    cgrp_curve_t *crv;
    const char   *func, *svg;
    char         *end;
    token_t      *rpn;
    double        cmin, cmax, x, step;
    int           imin, imax, omin, omax, i, mapped, clamped; 
    int           opt;



#define OPTIONS "c:C:i:I:o:O:s:f:g:h"
    struct option options[] = {
        { "cmin", required_argument, NULL, 'c' },
        { "cmax", required_argument, NULL, 'C' },
        { "imin", required_argument, NULL, 'i' },
        { "imax", required_argument, NULL, 'I' },
        { "omin", required_argument, NULL, 'o' },
        { "omax", required_argument, NULL, 'O' },
        { "step", required_argument, NULL, 's' },
        { "func", required_argument, NULL, 'f' },
        { "svg" , required_argument, NULL, 'g' },
        { "help", no_argument      , NULL, 'h' },
        { NULL  , 0                , NULL,  0  }
    };

    
    curve_init(NULL);

    func = "1 / 10 * 2 ^(x / 10)";
    cmin = -10;
    cmax =  10;
    imin = -10;
    imax =  10;
    step = 0.5;
    omin = -17;
    omax =  15;
    svg  =  NULL;
    
    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            printf("%s [--cmin cmin] [--cmax cmax] [--step step] --func func\n"
                   "   [--imin imin] [--imax imax] "
                   "[--omin omin] [--omax omax] [--svg out]\n",
                   argv[0]);
            exit(0);
            break;

        case 'c':
            errno = 0;
            cmin = strtod(optarg, &end);
            if (errno != 0 || *end)
                fatal("invalid cmin argument '%s'", optarg);
            break;
            
        case 'C':
            errno = 0;
            cmax = strtod(optarg, &end);
            if (errno != 0 || *end)
                fatal("invalid cmax argument '%s'", optarg);
            break;

        case 'i':
            errno = 0;
            imin = strtoul(optarg, &end, 10);
            if (errno != 0 || *end)
                fatal("invalid imin argument '%s'", optarg);
            break;
            
        case 'I':
            errno = 0;
            imax = strtoul(optarg, &end, 10);
            if (errno != 0 || *end)
                fatal("invalid imax argument '%s'", optarg);
            break;

        case 'o':
            errno = 0;
            omin = strtoul(optarg, &end, 10);
            if (errno != 0 || *end)
                fatal("invalid omin argument '%s'", optarg);
            break;
            
        case 'O':
            errno = 0;
            omax = strtoul(optarg, &end, 10);
            if (errno != 0 || *end)
                fatal("invalid omax argument '%s'", optarg);
            break;

        case 's':
            errno = 0;
            step = strtod(optarg, &end);
            if (errno != 0 || *end)
                fatal("invalid step argument '%s'", optarg);
            break;

        case 'f':
            func = optarg;
            break;

        case 'g':
            svg = optarg;
            break;
            
        default:
            fatal("unknown command line option '%c'", opt);
        }
    }

    rpn = rpn_parse(func);
    
    if (rpn == NULL)
        fatal("failed to parse function definition '%s'", func);
    
    for (x = cmin; x <= cmax; x += step)
        printf("f(%f) = %f\n", x, rpn_calc(x, rpn));
    
    rpn_free(rpn);

    crv = curve_create(func, cmin, cmax, imin, imax, omin, omax);
    
    if (crv == NULL)
        fatal("failed to create curve '%s'", func);

    for (i = imin - (imax - imin) / 2; i <= imax + (imax - imin) / 2; i++) {
        mapped = curve_map(crv, i, &clamped);
        printf("curve(%d:%d) = %d\n", i, clamped, mapped);
    }

    if (svg != NULL)
        curve_to_svg(crv, svg, imin, imax);
    
    curve_destroy(crv);

    return 0;
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


