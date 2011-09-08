%{ /* -*- c -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "cgrp-plugin.h"
#include "cgrp-parser-types.h"
#include "cgrp-config.h"

#include "mm.h"

    
#define DEBUG(format, args...) do {            \
        if (lexer.debug)                       \
            printf("D: "format"\n" , ## args); \
    } while (0)
    

#define IGNORE(type) do {                                               \
        DEBUG("ignored %s @ %s:%d", #type, lexer_file(), lexer_line()); \
    } while (0)


/*****************************************************************************
 *                *** lexical analyser type definitions ***                  *
 *****************************************************************************/

#define RINGBUF_SIZE (16 * 1024)               /* token ring buffer size */


enum {
    INPUT_TYPE_FILE = 0,                       /* a single input file */
    INPUT_TYPE_GLOB,                           /* a shell-like pattern */
};


typedef struct lexer_input_s lexer_input_t;

#define LEXER_INPUT_COMMON                                                    \
    lexer_input_t   *prev;                     /* previous input */           \
    int              type;                     /* INPUT_TYPE_* */             \
    YY_BUFFER_STATE  yybuf;                    /* (f)lex input buffer */      \
    FILE            *fp;                       /* input stream */             \
    char            *file;                     /* input file path */          \
    int              line                      /* input line number */        \


typedef struct {                               /* a single input file */
    LEXER_INPUT_COMMON;
} lexer_file_t;


typedef struct {                               /* an input glob */
    LEXER_INPUT_COMMON;
    char    *dir;                              /* directory path */
    char    *pattern;                          /* file pattern */
    DIR     *dp;                               /* directory handle */
    regex_t  regex;                            /* globbing pattern */
} lexer_glob_t;


struct lexer_input_s {
    LEXER_INPUT_COMMON;
};


typedef struct {
    int              debug;                    /* debug lexical analysis ? */
    int              eol;                      /* last token EOL ? */
    int              eof;                      /* previous token EOF ? */
    char             tokens[RINGBUF_SIZE];     /* token ring buffer */
    int              offset;                   /* ring buffer offset */
    lexer_input_t   *input;                    /* current input */
} lexer_t;


/*
 * token types
 */

typedef enum {
    TOKEN_TYPE_UNKNOWN = 0,
    TOKEN_TYPE_IDENT,                          /* identifier */
    TOKEN_TYPE_PATH,                           /* file system path */
    TOKEN_TYPE_STRING,                         /* quoted string */
    TOKEN_TYPE_ARG,                            /* argument */
    TOKEN_TYPE_UINT,                           /* unsigned integer */
    TOKEN_TYPE_SINT,                           /* signed integer */
    TOKEN_TYPE_DOUBLE,                         /* double prec. floating */
    TOKEN_TYPE_EOL,                            /* end of line */
} token_type_t;

static lexer_t lexer = { .debug = 0 };
static int     lexer_start_token;              /* parser selector token */
static int     lexer_noinclude;                /* disable includes */

/*****************************************************************************
 *                    *** token ring buffer management ***                   *
 *****************************************************************************/

static char *
printable_token(char *token)
{
    static char buf[PATH_MAX], *p, *q;
    int         n;

    p = token;
    q = buf;
    n = sizeof(buf) - 1;
    while (*p && n > 0) {
        switch (*p) {
        case '\n':
        case '\r':
            if (n > 2) {
                *q++ = '\\';
                *q++ = *p == '\n' ? 'n': 'r';
                n -= 2;
            }
            else {
                *q++ = '.';
                n--;
            }
            break;
        default:
            *q++ = *p;
            n--;
        }
        p++;
    }
    *q = '\0';
    
    return buf;
}


static char *
token_saven(lexer_t *l, char *token, int length)
{
    int   size;
    char *saved;

    size = strlen(token);
    
    if (length > size)
        length = size;

    size = length + 1;

    if (l->offset + size >= RINGBUF_SIZE)
        l->offset = 0;
    
    saved = l->tokens + l->offset;
    l->offset += size;

    strncpy(saved, token, length);
    saved[length] = '\0';

    cgrpyylval.any.token  = saved;
    cgrpyylval.any.lineno = l->input->line;
    DEBUG("SAVED '%s'", printable_token(saved));

    return saved;
}

static char *
token_save(lexer_t *l, char *token)
{
    return token_saven(l, token, strlen(token));
}


/*****************************************************************************
 *                      *** token parsing & passing ***                      *
 *****************************************************************************/

static void
lexer_error(lexer_t *l, const char *format, ...)
{
    va_list ap;
    
    OHM_ERROR("cgrp: lexical error on line %d in file %s",
              l->input->line, l->input->file);
    
    va_start(ap, format);
    ohm_logv(OHM_LOG_ERROR, format, ap);
    va_end(ap);

    exit(1);
}


#define PASS_KEYWORD(kw) do {                                           \
        char *__token;                                                  \
                                                                        \
        DEBUG("KEYWORD %s @ %s:%d", #kw, lexer_file(), lexer_line());   \
                                                                        \
        __token = token_save(&lexer, cgrpyytext);                       \
        lexer.eol = FALSE;                                              \
                                                                        \
        if (KEYWORD_##kw == KEYWORD_CLASSIFY_ARGVX) {                   \
            __token += sizeof("classify-by-argv") - 1;                  \
            cgrpyylval.uint32.value = parse_uint32(&lexer, __token);    \
        }                                                               \
                                                                        \
        cgrpyylval.any.token = __token;                                 \
        return KEYWORD_##kw;                                            \
    } while (0)

#define PASS_TOKEN(type) do {                                           \
        char *__token;                                                  \
                                                                        \
        if (!strcmp(#type, "EOL"))                                      \
            lexer_error(&lexer, "internal error: TOKEN(EOL) !");        \
                                                                        \
        DEBUG("TOKEN %s @ %s:%d", #type, lexer_file(), lexer_line());   \
                                                                        \
        lexer.eol = FALSE;                                              \
        __token = token_save(&lexer, cgrpyytext);                       \
                                                                        \
        cgrpyylval.any.token = __token;                                 \
        return TOKEN_##type;                                            \
    } while (0)
    

#define PASS_NUMBER(type) do {                                          \
        char *__token;                                                  \
                                                                        \
        DEBUG("NUMBER %s @ %s:%d", #type, lexer_file(), lexer_line());  \
                                                                        \
        lexer.eol = FALSE;                                              \
        __token = token_save(&lexer, cgrpyytext);                       \
                                                                        \
        switch (TOKEN_TYPE_##type) {                                    \
        case TOKEN_TYPE_ARG:                                            \
            __token += sizeof("arg") - 1;                               \
            cgrpyylval.uint32.value = parse_uint32(&lexer, __token);    \
            break;                                                      \
        case TOKEN_TYPE_UINT:                                           \
            cgrpyylval.uint32.value = parse_uint32(&lexer, __token);    \
            break;                                                      \
        case TOKEN_TYPE_SINT:                                           \
            cgrpyylval.sint32.value = parse_sint32(&lexer, __token);    \
            break;                                                      \
        case TOKEN_TYPE_DOUBLE:                                         \
            cgrpyylval.dbl.value = parse_double(&lexer, __token);       \
            break;                                                      \
        default:                                                        \
            lexer_error(&lexer, "invalid numeric type %s", #type);      \
        }                                                               \
                                                                        \
        cgrpyylval.any.token = __token;                                 \
        return TOKEN_##type;                                            \
    } while (0)

#define PASS_STRING(type) do {                                          \
        char *__token, *__value;                                        \
        int   __len;                                                    \
                                                                        \
        DEBUG("TOKEN %s @ %s:%d", #type, lexer_file(), lexer_line());   \
                                                                        \
        __len   = cgrpyyleng;                                           \
        __value = cgrpyytext;                                           \
                                                                        \
        if (TOKEN_TYPE_##type == TOKEN_TYPE_STRING) {                   \
            if (__value[0] == '"' || __value[0] == '\'')                \
                __token = token_saven(&lexer, __value + 1, __len - 2);  \
            else                                                        \
                __token = token_save(&lexer, __value);                  \
        }                                                               \
        else                                                            \
            __token = token_save(&lexer, __value);                      \
                                                                        \
        cgrpyylval.string.value = __token;                              \
        lexer.eol = FALSE;                                              \
                                                                        \
        cgrpyylval.any.token = __token;                                 \
        return TOKEN_##type;                                            \
    } while (0)

#define PROCESS_EOL() do {                                              \
        char *__token;                                                  \
                                                                        \
        lexer.input->line++;                                            \
        if (lexer.eol) 							\
            IGNORE(EOL);                                                \
        else {                                                          \
            DEBUG("TOKEN EOL @ %s:%d", lexer_file(), lexer_line());     \
                                                                        \
            lexer.eol = TRUE;                                           \
            __token = token_saven(&lexer, "\n", 1);                     \
                                                                        \
            cgrpyylval.any.token = __token;                             \
            return TOKEN_EOL;                                           \
        }                                                               \
    } while (0)

#define PUSH_BACK_EOL() do {                                            \
        lexer.input->line--;                                            \
        unput('\n');                                                    \
    } while (0)


static inline u32_t
parse_uint32(lexer_t *l, char *str)
{
    u32_t  value;
    char  *end;

    value = (u32_t)strtoull(str, &end, 10);
    
    if (*end != '\0')
        lexer_error(l, "invalid unsigned integer \"%s\"", str);
    
    return value;
}


static inline s32_t
parse_sint32(lexer_t *l, char *str)
{
    s32_t  value;
    char  *end;

    value = (s32_t)strtoll(str, &end, 10);
    
    if (*end != '\0')
        lexer_error(l, "invalid signed integer \"%s\"", str);
    
    return value;
}


static inline double
parse_double(lexer_t *l, char *str)
{
    double  value;
    char   *end;

    value = strtod(str, &end);
    
    if (*end != '\0')
        lexer_error(l, "invalid double precision floating \"%s\"", str);
    
    return value;
}


/*****************************************************************************
 *                  *** lexical analyzer public interface ***                *
 *****************************************************************************/

static lexer_input_t *lexer_open_file (char *path);
static void           lexer_close_file(lexer_file_t *input);
static lexer_input_t *lexer_open_glob (char *path);
static void           lexer_close_glob(lexer_glob_t *input);
static int            lexer_more_glob(lexer_glob_t *input);


int
lexer_push_input(char *path)
{
    lexer_input_t *input;
    
    if (getenv("CGRP_LEXER_DEBUG") != NULL)
        lexer.debug = TRUE;
    
    if (strchr(path, '*') || strchr(path, '?'))
        input = lexer_open_glob(path);
    else
        input = lexer_open_file(path);

    if (input != NULL) {
        input->prev = lexer.input;
        lexer.input = input;
        
        input->yybuf = cgrpyy_create_buffer(input->fp, YY_BUF_SIZE);
        cgrpyy_switch_to_buffer(input->yybuf);
        lexer.eol = TRUE;
        return TRUE;
    }
    else {
        OHM_ERROR("cgrp: failed to open input '%s'", path);
        return FALSE;
    }
}


int
lexer_pop_input(void)
{
    lexer_input_t *input, *prev;

    if (lexer.input == NULL)
        return FALSE;

    input = lexer.input;
    prev  = input->prev;

    cgrpyy_delete_buffer(input->yybuf);

    if (input->type == INPUT_TYPE_GLOB) {
        if (lexer_more_glob((lexer_glob_t *)input)) {
            input->yybuf = cgrpyy_create_buffer(input->fp, YY_BUF_SIZE);
            cgrpyy_switch_to_buffer(input->yybuf);
            
            return TRUE;
        }
        
        lexer_close_glob((lexer_glob_t *)input);
    }
    else
        lexer_close_file((lexer_file_t *)input);

    lexer.input = prev;
    
    if (prev != NULL) {
        cgrpyy_switch_to_buffer(prev->yybuf);
        lexer.eol = TRUE;
        return TRUE;
    }
    
    return FALSE;
}


void
lexer_debug(int state)
{
    lexer.debug = state;
}


int
lexer_line(void)
{
    return lexer.input ? lexer.input->line : 0;
}


const char *
lexer_file(void)
{
    return lexer.input ? lexer.input->file : "<unknown>";
}


void
lexer_reset(int start_token)
{
    while (lexer_pop_input())
        ;

    lexer_start_token = start_token;
    lexer_noinclude   = FALSE;
}


void
lexer_disable_include(void)
{
    lexer_noinclude = TRUE;
}


void
lexer_enable_include(void)
{
    lexer_noinclude = FALSE;
}


static lexer_input_t *
lexer_open_file(char *path)
{
    lexer_file_t *input;

    if (ALLOC_OBJ(input) == NULL) {
        OHM_ERROR("cgrp: failed to allocate memory for new input %s", path);
        exit(1);
    }

    input->type = INPUT_TYPE_FILE;
    input->fp   = fopen(path, "r");

    if (input->fp == NULL) {
        OHM_ERROR("cgrp: failed to open input file '%s'", path);
        lexer_close_file(input);
        return NULL;
    }

    input->file = STRDUP(path);
    input->line = 1;

    DEBUG("opened file %s for parsing...", path);
    
    return (lexer_input_t *)input;
}


static void
lexer_close_file(lexer_file_t *input)
{
    if (input == NULL)
        return;
    
    if (input->fp)
        fclose(input->fp);
    FREE(input->file);
    
    FREE(input);
}


static lexer_input_t *
lexer_open_glob(char *path)
{
    lexer_glob_t *input;
    char         *base, *p, pattern[PATH_MAX], *q;
    int           len;

    if (ALLOC_OBJ(input) == NULL) {
        OHM_ERROR("cgrp: failed to allocate memory for input '%s'", path);
        return NULL;
    }
    
    if ((base = strrchr(path, '/')) != NULL) {
        if (((p = strchr(path, '*')) != NULL && p < base) ||
            ((p = strchr(path, '?')) != NULL && p < base)) {
            OHM_ERROR("cgrp: invalid include glob '%s'", path);
            lexer_close_glob(input);
            return NULL;
        }

        for (p = base; *p == '/'; p++)
            ;
        while (*base == '/' && base > path)
            base--;

        len        = base - path + 1;
        input->dir = ALLOC_ARR(char, len + 1);
        strncpy(input->dir, path, len);
        input->dir[len] = '\0';

        input->pattern = STRDUP(p);
    }
    else {
        input->dir     = STRDUP(".");
        input->pattern = STRDUP(path);
    }
    
    
    for (p = input->pattern, q = pattern; *p; p++) {
        switch (*p) {
        case '*': *q++ = '.';  *q++ = '*'; break;
        case '?': *q++ = '.';              break;
        case '.': *q++ = '\\'; *q++ = '.'; break;
        default:  *q++ = *p;               break;
        }
    }
    *q = '\0';

    if (regcomp(&input->regex, pattern, REG_NOSUB | REG_NEWLINE) != 0) {
        OHM_ERROR("cgrp: failed to compile regexp '%s' for '%s'",
                  pattern, input->pattern);
        lexer_close_glob(input);
        return NULL;
    }
    
    input->type = INPUT_TYPE_GLOB;
    input->dp   = opendir(input->dir);
    
    if (input->dp == NULL) {
        OHM_ERROR("cgrp: failed to open directory '%s'", input->dir);
        lexer_close_glob(input);
        return NULL;
    }

    if (lexer_more_glob(input) == FALSE) {
        lexer_close_glob(input);
        return NULL;
    }

    return (lexer_input_t *)input;
}


static void
lexer_close_glob(lexer_glob_t *input)
{
    if (input == NULL)
        return;

    FREE(input->dir);
    FREE(input->pattern);
    FREE(input->file);

    if (input->fp)
        fclose(input->fp);
    
    if (input->dp) {
        closedir(input->dp);
        input->dp = NULL;
    }

    regfree(&input->regex);
    
    FREE(input);
}
 

static int
lexer_more_glob(lexer_glob_t *input)
{
    struct dirent *de;
    struct stat    st;
    char           path[PATH_MAX];
    regmatch_t     m;

    FREE(input->file);
    input->file = NULL;
    if (input->fp != NULL) {
        fclose(input->fp);
        input->fp = NULL;
    }
    
    while ((de = readdir(input->dp)) != NULL) {
        snprintf(path, sizeof(path), "%s/%s", input->dir, de->d_name);
        DEBUG("found input candidate %s...", path);

        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;

        if (!regexec(&input->regex, de->d_name, 1, &m, REG_NOTBOL|REG_NOTEOL)) {
            if ((input->fp = fopen(path, "r")) != NULL) {
                input->file = STRDUP(path);

                DEBUG("opened file %s for parsing...", path);
                
                return TRUE;
            }
        }
    }
    
    return FALSE;
}




/*****************************************************************************
 *                      *** miscallaneous lexer routines ***                 *
 *****************************************************************************/

static inline int
cgrpyywrap(void)
{
    return 1;
}


%}

EOL                       \n
WHITESPACE                [ \t]+

INCLUDE                  ^#include[ \t]+'[^\n']*'$
COMMENT_FULL             ^[ \t]*#.*$
COMMENT_TRAIL             [ \t]*#.*$

KEYWORD_GLOBAL            \[global\]
KEYWORD_PARTITION         partition
KEYWORD_DESCRIPTION       description
KEYWORD_PATH              path
KEYWORD_CPU_SHARES        cpu-shares
KEYWORD_REALTIME_LIMIT    realtime-limit
KEYWORD_MEM_LIMIT         memory-limit
KEYWORD_RULE              rule
KEYWORD_BINARY            binary
KEYWORD_CMDLINE           commandline
KEYWORD_NAME              name
KEYWORD_GROUP             group
KEYWORD_RENICE            renice
KEYWORD_SCHEDULE          schedule
KEYWORD_TYPE              type
KEYWORD_USER              user
KEYWORD_PARENT            parent
KEYWORD_IGNORE            ignore
KEYWORD_LEADS             leads
KEYWORD_RECLASS_AFTER     reclassify-after
KEYWORD_RECLASSIFY        reclassify-count
KEYWORD_CLASSIFY_ARGVX    classify-by-argv[0-9]+
KEYWORD_CLASSIFY          classify
KEYWORD_PRIORITY          priority
KEYWORD_OOM               out-of-memory
KEYWORD_RESPONSE_CURVE    response-curve
KEYWORD_NO_OP             no-op
KEYWORD_EXPORT_GROUPS     export-group-facts
KEYWORD_EXPORT_PARTITIONS export-partition-facts
KEYWORD_EXPORT_FACT       export-fact
KEYWORD_CGROUPFS_OPTIONS  cgroupfs-options
KEYWORD_IOWAIT_NOTIFY     iowait-notify
KEYWORD_IOQLEN_NOTIFY     ioqlen-notify
KEYWORD_SWAP_PRESSURE     swap-pressure
KEYWORD_ADDON_RULES       addon-rules
KEYWORD_CGROUP_CONTROL    cgroup-control
KEYWORD_ALWAYS_FALLBACK   always-fallback
KEYWORD_PRESERVE_PRIO     preserve-priority

HEADER_OPEN            \[
HEADER_CLOSE           \]
CURLY_OPEN             \{
CURLY_CLOSE            \}

ARG                    arg[0-9]+

EQUAL                  ==
NOTEQ                  !=
AND                    &&
OR                     \|\|
NOT                    \!
IMPLIES                =>
SEMICOLON              ;
COMMA                  ,
COLON                  :
LESS                   <
GREATER                >

ASTERISK               \*
PATH                   (\/([a-zA-Z_]([a-zA-Z0-9_\.-]*)*))+
IDENT                  [a-zA-Z_]([a-zA-Z0-9_-]*[a-zA-Z0-9_])*
STRING                 (('[^\n']*')|(\"[^\n\"]*\"))
DOUBLE	               [+-]?[0-9]+\.[0-9]+
UINT                   [0-9]+
SINT                   [+|-]?[0-9]+
ESCAPE		       \\

%%

%{

    /*
     * Notes:
     *   To avoid having to separate parsers, we need two entry points to
     *   the parser. The first one is for parsing a full configuration file
     *   including global options, partitions, groups, and rules. The second
     *   is for parsing an additional set of add-on classification rules.
     *
     *   To accomplish this while still being able to share the productions of
     *   the two parsers we
     *     1) fork the parser at the top level production
     *     2) have the parser control which parser is to be used (lexer_type)
     *     3) have the lexer emit type-specific start-symbols to activate the
     *        correct parser
     */
    
    if (lexer_start_token != 0) {
        int start_token;

        start_token       = lexer_start_token;
        lexer_start_token = 0;

        return start_token;
    }
%}


{INCLUDE}                   { char path[PATH_MAX], *beg, *end;
                              int len;

			      if (lexer_noinclude)
			          return 1;                              
                              beg = strchr(yytext, '\''); beg++;
                              end = strchr(beg,    '\''); end--;
                              len = end - beg + 1;
                              if (len >= PATH_MAX)
                                  OHM_ERROR("cgrp: too long path '%s'", beg-1);
                              else {
                                  strncpy(path, beg, len);
                                  path[len] = '\0';
                                  lexer_push_input(path);
                              }
                            }
                              
                              

{WHITESPACE}                { IGNORE(WHITESPACE);              }
{COMMENT_FULL}              { IGNORE(COMMENT_FULL);
                              PUSH_BACK_EOL();                 }
{COMMENT_TRAIL}             { IGNORE(COMMENT_TRAIL);
                              PUSH_BACK_EOL();                 }
\r                          { IGNORE(CR);                      }
{EOL}                       { PROCESS_EOL();                   }

{KEYWORD_GLOBAL}            { PASS_KEYWORD(GLOBAL);            }
{KEYWORD_PARTITION}         { PASS_KEYWORD(PARTITION);         }
{KEYWORD_DESCRIPTION}       { PASS_KEYWORD(DESCRIPTION);       }
{KEYWORD_CPU_SHARES}        { PASS_KEYWORD(CPU_SHARES);        }
{KEYWORD_MEM_LIMIT}         { PASS_KEYWORD(MEM_LIMIT);         }
{KEYWORD_REALTIME_LIMIT}    { PASS_KEYWORD(REALTIME_LIMIT);    }
{KEYWORD_PATH}              { PASS_KEYWORD(PATH);              }
{KEYWORD_RULE}              { PASS_KEYWORD(RULE);              }
{KEYWORD_GROUP}             { PASS_KEYWORD(GROUP);             }
{KEYWORD_RENICE}            { PASS_KEYWORD(RENICE);            }
{KEYWORD_SCHEDULE}          { PASS_KEYWORD(SCHEDULE);          }
{KEYWORD_USER}              { PASS_KEYWORD(USER);              }
{KEYWORD_PARENT}            { PASS_KEYWORD(PARENT);            }
{KEYWORD_BINARY}            { PASS_KEYWORD(BINARY);            }
{KEYWORD_CMDLINE}           { PASS_KEYWORD(CMDLINE);           }
{KEYWORD_NAME}              { PASS_KEYWORD(NAME);              }
{KEYWORD_TYPE}              { PASS_KEYWORD(TYPE);              }
{KEYWORD_IGNORE}            { PASS_KEYWORD(IGNORE);            }
{KEYWORD_LEADS}             { PASS_KEYWORD(LEADS);             }
{KEYWORD_RECLASS_AFTER}     { PASS_KEYWORD(RECLASS_AFTER);     }
{KEYWORD_RECLASSIFY}        { PASS_KEYWORD(RECLASSIFY);        }
{KEYWORD_CLASSIFY}          { PASS_KEYWORD(CLASSIFY);          }
{KEYWORD_CLASSIFY_ARGVX}    { PASS_KEYWORD(CLASSIFY_ARGVX);    }
{KEYWORD_PRIORITY}          { PASS_KEYWORD(PRIORITY);          }
{KEYWORD_OOM}               { PASS_KEYWORD(OOM);               }
{KEYWORD_RESPONSE_CURVE}    { PASS_KEYWORD(RESPONSE_CURVE);    }
{KEYWORD_NO_OP}             { PASS_KEYWORD(NO_OP);             }
{KEYWORD_EXPORT_GROUPS}     { PASS_KEYWORD(EXPORT_GROUPS);     }
{KEYWORD_EXPORT_PARTITIONS} { PASS_KEYWORD(EXPORT_PARTITIONS); }
{KEYWORD_EXPORT_FACT}       { PASS_KEYWORD(EXPORT_FACT);       }
{KEYWORD_CGROUPFS_OPTIONS}  { PASS_KEYWORD(CGROUPFS_OPTIONS);  }
{KEYWORD_CGROUP_CONTROL}    { PASS_KEYWORD(CGROUP_CONTROL);    }
{KEYWORD_IOWAIT_NOTIFY}     { PASS_KEYWORD(IOWAIT_NOTIFY);     }
{KEYWORD_IOQLEN_NOTIFY}     { PASS_KEYWORD(IOQLEN_NOTIFY);     }
{KEYWORD_SWAP_PRESSURE}     { PASS_KEYWORD(SWAP_PRESSURE);     }
{KEYWORD_ADDON_RULES}       { PASS_KEYWORD(ADDON_RULES);       }
{KEYWORD_ALWAYS_FALLBACK}   { PASS_KEYWORD(ALWAYS_FALLBACK);   }
{KEYWORD_PRESERVE_PRIO}     { PASS_KEYWORD(PRESERVE_PRIO);     }

{HEADER_OPEN}               { PASS_TOKEN(HEADER_OPEN);         }
{HEADER_CLOSE}              { PASS_TOKEN(HEADER_CLOSE);        }

{ARG}                       { PASS_NUMBER(ARG);                }
{EQUAL}                     { PASS_TOKEN(EQUAL);               }
{NOTEQ}                     { PASS_TOKEN(NOTEQ);               }
{LESS}                      { PASS_TOKEN(LESS);                }
{GREATER}                   { PASS_TOKEN(GREATER);             }
{CURLY_OPEN}                { PASS_TOKEN(CURLY_OPEN);          }
{CURLY_CLOSE}               { PASS_TOKEN(CURLY_CLOSE);         }

{AND}                       { PASS_TOKEN(AND);                 }
{OR}                        { PASS_TOKEN(OR);                  }
{NOT}                       { PASS_TOKEN(NOT);                 }
{IMPLIES}                   { PASS_TOKEN(IMPLIES);             }
{SEMICOLON}                 { PASS_TOKEN(SEMICOLON);           }
{COMMA}                     { PASS_TOKEN(COMMA);               }
{COLON}                     { PASS_TOKEN(COLON);               }

{ASTERISK}                  { PASS_TOKEN(ASTERISK);            }
{IDENT}                     { PASS_STRING(IDENT);              }
{PATH}                      { PASS_STRING(PATH);               }
{STRING}                    { PASS_STRING(STRING);             }
{DOUBLE}                    { PASS_NUMBER(DOUBLE);             }
{UINT}                      { PASS_NUMBER(UINT);               }
{SINT}                      { PASS_NUMBER(SINT);               }

{ESCAPE}		    { int c;
			      if ((c = input()) != '\n')
			          unput(c);
			      else {
			          DEBUG("escaped \\n @ %s:%d",
				        lexer_file(), lexer_line());
			          lexer.input->line++;
			      }
			    }

<<EOF>>                     {
                               if (!lexer.eof) {
                                   lexer.eof = TRUE;
                                   PROCESS_EOL();
                               }
                               else {
                                   lexer.eof = FALSE;
                                   if (!lexer_pop_input())
                                       yyterminate();
                               }
/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
                                                               }
