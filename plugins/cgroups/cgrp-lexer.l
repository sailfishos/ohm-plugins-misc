%{

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "cgrp-plugin.h"
#include "cgrp-parser-types.h"
#include "cgrp-config.h"



#define DEBUG(format, args...) do {                                     \
        if (lexer.debug)                                                \
            printf("D: "format" on line %d\n" , ## args, lexer.line);   \
    } while (0)
    

#define IGNORE(type) do {                              \
        DEBUG("ignored %s", #type); \
    } while (0)


/*****************************************************************************
 *                *** lexical analyser type definitions ***                  *
 *****************************************************************************/

#define RINGBUF_SIZE (16 * 1024)               /* token ring buffer size */

typedef struct {
    YY_BUFFER_STATE  yybuf;                    /* (f)lex input buffer */
    const char      *file;                     /* input file */
    int              line;                     /* input line number */
    int              debug;                    /* debug lexical analysis ? */
    int              eol;                      /* last token EOL ? */
    char             tokens[RINGBUF_SIZE];     /* token ring buffer */
    int              offset;                   /* ring buffer offset */
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
    TOKEN_TYPE_EOL,                            /* end of line */
} token_type_t;


static lexer_t lexer;


/*****************************************************************************
 *                    *** token ring buffer management ***                   *
 *****************************************************************************/

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
    cgrpyylval.any.lineno = l->line;
    DEBUG("SAVE '%s'", saved);

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
    
    OHM_ERROR("cgrp: lexical error on line %d in file %s", l->line, l->file);
    
    va_start(ap, format);
    ohm_logv(OHM_LOG_ERROR, format, ap);
    va_end(ap);

    exit(1);
}



#define PASS_KEYWORD(kw) do {                   \
        DEBUG("KEYWORD %s", #kw);               \
                                                \
        token_save(&lexer, cgrpyytext);         \
        lexer.eol = FALSE;                      \
                                                \
        return KEYWORD_##kw;                    \
    } while (0)

#define PASS_TOKEN(type) do {                                           \
        if (!strcmp(#type, "EOL"))                                      \
            lexer_error(&lexer, "internal error: TOKEN(EOL) !");        \
                                                                        \
        DEBUG("TOKEN %s", #type);                                       \
                                                                        \
        lexer.eol = FALSE;                                              \
        token_save(&lexer, cgrpyytext);                                 \
                                                                        \
        return TOKEN_##type;                                            \
    } while (0)
    
#define PASS_NUMBER(type) do {                                          \
        char *__token;                                                  \
                                                                        \
        DEBUG("NUMBER %s", #type);                                      \
                                                                        \
        lexer.eol = FALSE;                                              \
        __token = token_save(&lexer, cgrpyytext);                       \
                                                                        \
        switch (TOKEN_TYPE_##type) {                                    \
        case TOKEN_TYPE_ARG:                                            \
            cgrpyylval.uint32.value = parse_uint32(&lexer, __token+3);  \
            break;                                                      \
        case TOKEN_TYPE_UINT:                                           \
            cgrpyylval.uint32.value = parse_uint32(&lexer, __token);    \
            break;                                                      \
        case TOKEN_TYPE_SINT:                                           \
            cgrpyylval.sint32.value = parse_sint32(&lexer, __token);    \
            break;                                                      \
        default:                                                        \
            lexer_error(&lexer, "invalid numeric type %s", #type);      \
        }                                                               \
                                                                        \
        return TOKEN_##type;                                            \
    } while (0)

#define PASS_STRING(type) do {                                          \
        char *__token, *__value;                                        \
        int   __len;                                                    \
                                                                        \
        DEBUG("TOKEN %s", #type);                                       \
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
        return TOKEN_##type;                                            \
    } while (0)

#define PROCESS_EOL() do {                                              \
        lexer.line++;                                                   \
        if (lexer.eol) 							\
            IGNORE(EOL);                                                \
        else {                                                          \
            DEBUG("TOKEN EOL");                                         \
                                                                        \
            lexer.eol = TRUE;                                           \
            token_saven(&lexer, "\n", 1);                               \
                                                                        \
            return TOKEN_EOL;                                           \
        }                                                               \
    } while (0)

#define PUSH_BACK_EOL() do {                                            \
        lexer.line--;                                                   \
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


/*****************************************************************************
 *                  *** lexical analyzer public interface ***                *
 *****************************************************************************/

int
lexer_open(FILE *fp, const char *path)
{
    lexer.yybuf = cgrpyy_create_buffer(fp, YY_BUF_SIZE);
    cgrpyy_switch_to_buffer(lexer.yybuf);
    
    lexer.file   = path;
    lexer.line   = 1;
    lexer.debug  = FALSE;
    lexer.eol    = TRUE;
    lexer.offset = 0;

    return TRUE;
}


void
lexer_close(void)
{
    cgrpyy_delete_buffer(lexer.yybuf);
    
    lexer.file   = "<none>";
    lexer.line   = 0;
    lexer.debug  = FALSE;
    lexer.eol    = FALSE;
    lexer.offset = 0;
}


void
lexer_debug(int state)
{
    lexer.debug = state;
}


int
lexer_line(void)
{
    return lexer.line;
}


const char *
lexer_file(void)
{
    return lexer.file;
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

COMMENT_FULL             ^[ \t]*#.*$
COMMENT_TRAIL             [ \t]*#.*$

KEYWORD_GLOBAL            \[global\]
KEYWORD_PARTITION         partition
KEYWORD_DESCRIPTION       description
KEYWORD_PATH              path
KEYWORD_CPU_SHARES        cpu-shares
KEYWORD_MEM_LIMIT         memory-limit
KEYWORD_RULE              rule
KEYWORD_BINARY            binary
KEYWORD_CMDLINE           commandline
KEYWORD_GROUP             group
KEYWORD_RENICE            renice
KEYWORD_TYPE              type
KEYWORD_USER              user
KEYWORD_PARENT            parent
KEYWORD_IGNORE            ignore
KEYWORD_RECLASS_AFTER     reclassify-after
KEYWORD_RECLASSIFY        reclassify-count
KEYWORD_EXPORT_GROUPS     export-group-facts
KEYWORD_EXPORT_PARTITIONS export-partition-facts
KEYWORD_EXPORT_FACT       export-fact
KEYWORD_CGROUPFS_OPTIONS  cgroupfs-options
KEYWORD_IOWAIT_NOTIFY     iowait-notify
KEYWORD_SWAP_PRESSURE     swap-pressure

HEADER_OPEN            \[
HEADER_CLOSE           \]

ARG                    arg[0-9]+

EQUAL                  ==
NOTEQ                  !=
LESS                   <
AND                    &&
OR                     \|\|
NOT                    \!
IMPLIES                =>

ASTERISK               \*
IDENT                  [a-zA-Z_]([a-zA-Z0-9_-]*[a-zA-Z0-9_])*
PATH                   (\/{IDENT})+
STRING                 (('[^\n']*')|(\"[^\n\"]*\"))
UINT                   [0-9]+
SINT                   [+|-]?[0-9]+

%%

{WHITESPACE}                { IGNORE(WHITESPACE);              }
{COMMENT_FULL}              { IGNORE(COMMENT_FULL);
                              PUSH_BACK_EOL();                 }
{COMMENT_TRAIL}             { IGNORE(COMMENT_TRAIL);
                              PUSH_BACK_EOL();                 }
{EOL}                       { PROCESS_EOL();                   }

{KEYWORD_GLOBAL}            { PASS_KEYWORD(GLOBAL);            }
{KEYWORD_PARTITION}         { PASS_KEYWORD(PARTITION);         }
{KEYWORD_DESCRIPTION}       { PASS_KEYWORD(DESCRIPTION);       }
{KEYWORD_CPU_SHARES}        { PASS_KEYWORD(CPU_SHARES);        }
{KEYWORD_MEM_LIMIT}         { PASS_KEYWORD(MEM_LIMIT);         }
{KEYWORD_PATH}              { PASS_KEYWORD(PATH);              }
{KEYWORD_RULE}              { PASS_KEYWORD(RULE);              }
{KEYWORD_GROUP}             { PASS_KEYWORD(GROUP);             }
{KEYWORD_RENICE}            { PASS_KEYWORD(RENICE);            }
{KEYWORD_USER}              { PASS_KEYWORD(USER);              }
{KEYWORD_PARENT}            { PASS_KEYWORD(PARENT);            }
{KEYWORD_BINARY}            { PASS_KEYWORD(BINARY);            }
{KEYWORD_CMDLINE}           { PASS_KEYWORD(CMDLINE);           }
{KEYWORD_TYPE}              { PASS_KEYWORD(TYPE);              }
{KEYWORD_IGNORE}            { PASS_KEYWORD(IGNORE);            }
{KEYWORD_RECLASS_AFTER}     { PASS_KEYWORD(RECLASS_AFTER);     }
{KEYWORD_RECLASSIFY}        { PASS_KEYWORD(RECLASSIFY);        }
{KEYWORD_EXPORT_GROUPS}     { PASS_KEYWORD(EXPORT_GROUPS);     }
{KEYWORD_EXPORT_PARTITIONS} { PASS_KEYWORD(EXPORT_PARTITIONS); }
{KEYWORD_EXPORT_FACT}       { PASS_KEYWORD(EXPORT_FACT);       }
{KEYWORD_CGROUPFS_OPTIONS}  { PASS_KEYWORD(CGROUPFS_OPTIONS);  }
{KEYWORD_IOWAIT_NOTIFY}     { PASS_KEYWORD(IOWAIT_NOTIFY);     }
{KEYWORD_SWAP_PRESSURE}     { PASS_KEYWORD(SWAP_PRESSURE);     }

{HEADER_OPEN}               { PASS_TOKEN(HEADER_OPEN);         }
{HEADER_CLOSE}              { PASS_TOKEN(HEADER_CLOSE);        }

{ARG}                       { PASS_NUMBER(ARG);                }
{EQUAL}                     { PASS_TOKEN(EQUAL);               }
{NOTEQ}                     { PASS_TOKEN(NOTEQ);               }
{LESS}                      { PASS_TOKEN(LESS);                }
{AND}                       { PASS_TOKEN(AND);                 }
{OR}                        { PASS_TOKEN(OR);                  }
{NOT}                       { PASS_TOKEN(NOT);                 }
{IMPLIES}                   { PASS_TOKEN(IMPLIES);             }

{ASTERISK}                  { PASS_TOKEN(ASTERISK);            }
{IDENT}                     { PASS_STRING(IDENT);              }
{PATH}                      { PASS_STRING(PATH);               }
{STRING}                    { PASS_STRING(STRING);             }
{UINT}                      { PASS_NUMBER(UINT);               }
{SINT}                      { PASS_NUMBER(SINT);               }

<<EOF>>                     { yyterminate();                   }
