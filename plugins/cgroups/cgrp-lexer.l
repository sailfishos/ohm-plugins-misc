%{

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "cgrp-plugin.h"
#include "cgrp-parser-types.h"
#include "cgrp-config.h"

#define DEBUG(format, args...) do {            \
        if (debug)                             \
            printf("D: "format"\n" , ## args); \
    } while (0)


#define IGNORE(type) do {                        \
        DEBUG("ignored %s", #type);              \
    } while (0)


typedef enum {
    UNKNOWN = 0,
    IDENT,
    PATH,
    STRING,
    ARG,
    UINT,
    SINT,
} lexer_type_t;


static void       lexer_error(int lineno, const char *format, ...);
static inline int cgrpyywrap(void);

static int debug = FALSE;
static int lineno;
static YY_BUFFER_STATE yybuf;


#define RINGBUFF_SIZE (16 * 1024)

static char ringbuff[RINGBUFF_SIZE];
static int  ringoffs = 0;

#define SAVE(v) ({                                      \
       int   __size = strlen(v) + 1;                    \
       char *__token;                                   \
                                                        \
       if (ringoffs + __size + 1 >= RINGBUFF_SIZE)      \
           ringoffs = 0;                                \
       __token = ringbuff + ringoffs;                   \
       ringoffs += __size;                              \
                                                        \
       strcpy(__token, v);                              \
       cgrpyylval.any.token  = __token;                 \
       cgrpyylval.any.lineno = lineno;                  \
       DEBUG("SAVE(\"%s\")", __token);                  \
       __token; })


#define SAVEN(v, n) ({                                     \
       int   __size, __slen;                               \
       char *__token;                                      \
                                                           \
       if ((n) > (__slen = strlen(v)))         		   \
           __size = __slen + 1;                            \
       else                                                \
           __size = (n) + 1;                               \
       if (ringoffs + __size + 1 >= RINGBUFF_SIZE)         \
           ringoffs = 0;                                   \
       __token = ringbuff + ringoffs;                      \
       ringoffs += __size;                                 \
                                                           \
       strncpy(__token, v, n);                             \
       __token[(n)] = '\0';                                \
       cgrpyylval.any.token  = __token;                    \
       cgrpyylval.any.lineno = lineno;                     \
       DEBUG("SAVE(\"%s\")", __token);                     \
       __token;                                            \
   })


#define TOKEN(type) do {                                             \
        DEBUG("TOKEN_%s", #type);                                    \
                                                                     \
        SAVE(cgrpyytext);                                            \
                                                                     \
        return TOKEN_##type;                                         \
    } while (0)


#define TOKEN_NUM(type) do {                                         \
        char *__token, *__value;                                     \
                                                                     \
        DEBUG("TOKEN_%s", #type);                                    \
                                                                     \
        __token = SAVE(cgrpyytext);                                  \
        __value = strchr(__token, ':');                              \
        __value = __value ? __value + 1 : __token;                   \
                                                                     \
        switch(type) {                                               \
        case ARG:                                                    \
	    cgrpyylval.uint32.value = PARSE_INT(u32_t, __value+3);   \
	    break;                                                   \
        case UINT:                                                   \
	    cgrpyylval.uint32.value = PARSE_INT(u32_t, __value);     \
	    break;                                                   \
        case SINT:                                                   \
	    cgrpyylval.uint32.value = PARSE_INT(s32_t, __value);     \
	    break;                                                   \
	default:                                                     \
            lexer_error(lineno, "invalid numeric type %s", #type);   \
        }                                                            \
                                                                     \
        return TOKEN_##type;                                         \
    } while (0)


#define TOKEN_STR(type) do {                                         \
        char *__token, *__value;                                     \
        int   __len;                                                 \
                                                                     \
        DEBUG("TOKEN_%s", #type);                                    \
                                                                     \
        __len    = cgrpyyleng;                                       \
        __value  = strchr(cgrpyytext, ':');                          \
        __value  = __value && __value != cgrpyytext ?                \
	           __value + 1 : cgrpyytext;                         \
        __len   -= (__value - cgrpyytext);                           \
        switch (type) {                                              \
        case STRING:                                                 \
            if (__value[0] == '\'' || __value[0] == '\"')            \
                __token = SAVEN(__value + 1, __len - 2);             \
            else                                                     \
                __token = SAVE(__value);                             \
            cgrpyylval.string.value = __token;                       \
            break;                                                   \
        case IDENT:                                                  \
        case PATH:                                                   \
            cgrpyylval.string.value = SAVE(__value);                 \
            break;                                                   \
	default:                                                     \
            lexer_error(lineno, "invalid string type %s", #type);    \
        }                                                            \
                                                                     \
        return TOKEN_##type;                                         \
    } while (0)


#define PARSE_INT(type, token) ({                                    \
        char *end;                                                   \
        type value;                                                  \
                                                                     \
        if (#type[0] == 's')                                         \
            value = (type)strtoll(token, &end, 10);                  \
        else                                                         \
            value = (type)strtoull(token, &end, 10);                 \
                                                                     \
        if (*end != '\0')                                            \
            lexer_error(lineno, "invalid integer \"%s\"", token);    \
                                                                     \
        value; })


int
lexer_init(FILE *fp)
{
    yybuf = cgrpyy_create_buffer(fp, YY_BUF_SIZE);
    cgrpyy_switch_to_buffer(yybuf);
    lineno = 1;

    return TRUE;
}

void
lexer_exit(void)
{
    cgrpyy_delete_buffer(yybuf);
}

void
lexer_debug(int state)
{
    debug = state;
}


static void
lexer_error(int lineno, const char *format, ...)
{
    va_list ap;

    if (lineno > 0)
        OHM_ERROR("cgrp: lexical error on line %d", lineno);
    else
        OHM_ERROR("cgrp: lexical error");

    va_start(ap, format);
    ohm_logv(OHM_LOG_ERROR, format, ap);
    va_end(ap);

    exit(1);
}

static inline int
cgrpyywrap(void)
{
    return 1;
}


%}

EOL               \n
WHITESPACE        [ \t]+

COMMENT_FULL      ^[ \t]*#.*$
COMMENT_TRAIL     [ \t]*#.*$

KW_GLOBAL         \[global\]
KW_PARTITION      partition
KW_DESCRIPTION    description
KW_PATH           path
KW_CPU_SHARES     cpu-shares
KW_MEM_LIMIT      memory-limit
KW_RULE           rule
KW_BINARY         binary
KW_CMDLINE        commandline
KW_GROUP          group
KW_RENICE         renice
KW_TYPE           type
KW_USER           user
KW_PARENT         parent
KW_IGNORE         ignore
KW_RECLASS_AFTER  reclassify-after
KW_RECLASSIFY     reclassify-count
KW_EXPORT_GROUPS  export-group-facts
KW_EXPORT_PARTS   export-partition-facts
KW_EXPORT_FACT    export-fact
KW_CGROUP_OPTIONS cgroupfs-options
KW_IOWAIT_NOTIFY  iowait-notify
KW_SWAP_PRESSURE  swap-pressure

HEADER_OPEN       \[
HEADER_CLOSE      \]

ARG               arg[0-9]+

EQUAL           ==
NOTEQ           !=
LESS            <
AND             &&
OR              \|\|
NOT             \!
IMPLIES         =>

ASTERISK        \*
IDENT           [a-zA-Z_]([a-zA-Z0-9_-]*[a-zA-Z0-9_])*
PATH            (\/{IDENT})+
STRING          (('[^\n']*')|(\"[^\n\"]*\"))
UINT            [0-9]+
SINT            [+|-]?[0-9]+

%%

{WHITESPACE}        { IGNORE(WHITESPACE);                 }
{COMMENT_FULL}      { IGNORE(COMMENT_FULL);  unput('\n'); }
{COMMENT_TRAIL}     { IGNORE(COMMENT_TRAIL); unput('\n'); }

{EOL}               { IGNORE(EOL); lineno++;              }

{KW_GLOBAL}         { TOKEN(KW_GLOBAL);        }
{KW_PARTITION}      { TOKEN(KW_PARTITION);     }
{KW_DESCRIPTION}    { TOKEN(KW_DESCRIPTION);   }
{KW_CPU_SHARES}     { TOKEN(KW_CPU_SHARES);    }
{KW_MEM_LIMIT}      { TOKEN(KW_MEM_LIMIT);     }
{KW_PATH}           { TOKEN(KW_PATH);          }
{KW_RULE}           { TOKEN(KW_RULE);          }
{KW_GROUP}          { TOKEN(KW_GROUP);         }
{KW_RENICE}         { TOKEN(KW_RENICE);        }
{KW_USER}           { TOKEN(KW_USER);          }
{KW_PARENT}         { TOKEN(KW_PARENT);        }
{KW_BINARY}         { TOKEN(KW_BINARY);        }
{KW_CMDLINE}        { TOKEN(KW_CMDLINE);       }
{KW_TYPE}           { TOKEN(KW_TYPE);          }
{KW_IGNORE}         { TOKEN(KW_IGNORE);        }
{KW_RECLASS_AFTER}  { TOKEN(KW_RECLASS_AFTER); }
{KW_RECLASSIFY}     { TOKEN(KW_RECLASSIFY);    }
{KW_EXPORT_GROUPS}  { TOKEN(KW_EXPORT_GROUPS); }
{KW_EXPORT_PARTS}   { TOKEN(KW_EXPORT_PARTS);  }
{KW_EXPORT_FACT}    { TOKEN(KW_EXPORT_FACT);   }
{KW_CGROUP_OPTIONS} { TOKEN(KW_CGROUP_OPTIONS);}
{KW_IOWAIT_NOTIFY}  { TOKEN(KW_IOWAIT_NOTIFY); }
{KW_SWAP_PRESSURE}  { TOKEN(KW_SWAP_PRESSURE); }

{HEADER_OPEN}       { TOKEN(HEADER_OPEN);    }
{HEADER_CLOSE}      { TOKEN(HEADER_CLOSE);   }

{ARG}               { TOKEN_NUM(ARG);        }

{EQUAL}             { TOKEN(EQUAL);          }
{NOTEQ}             { TOKEN(NOTEQ);          }
{LESS}              { TOKEN(LESS);           }
{AND}               { TOKEN(AND);            }
{OR}                { TOKEN(OR);             }
{NOT}               { TOKEN(NOT);            }
{IMPLIES}           { TOKEN(IMPLIES);        }

{ASTERISK}          { TOKEN(ASTERISK);       }
{IDENT}             { TOKEN_STR(IDENT);      }
{PATH}              { TOKEN_STR(PATH);       }
{STRING}            { TOKEN_STR(STRING);     }
{UINT}              { TOKEN_NUM(UINT);       }
{SINT}              { TOKEN_NUM(SINT);       }

<<EOF>>             { yyterminate();         }
