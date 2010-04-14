%{
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "plugin.h"
#include "config-private.h"
#include "config-parser.h"

#define EOL_TOKEN                                          \
    do {                                                   \
        debug_append("EOL", NULL);                         \
        debug_flush();                                     \
        yy_videoep_lineno++;                               \
        yy_videoep_column.first = 0;                       \
        yy_videoep_column.last  = 0;                       \
        yy_videoep_lval.string = "<eol>";                  \
        return TKN_EOL;                                    \
    } while(0)


#define EOF_TOKEN                                          \
    do {                                                   \
        debug_append("EOF", NULL);                         \
        debug_flush();                                     \
        yy_videoep_lineno++;                               \
        yy_videoep_column.first = 0;                       \
        yy_videoep_column.last  = 0;                       \
        yy_videoep_lval.string = "<eof>";                  \
        yyterminate();                                     \
    } while(0)


#define CONTINUATION_TOKEN                                 \
    do {                                                   \
        yy_videoep_lineno++;                               \
        yy_videoep_column.first = 0;                       \
        yy_videoep_column.last  = 0;                       \
    } while(0)


#define VALUELESS_TOKEN(t)                                 \
    do {                                                   \
        debug_append(#t, NULL);                            \
        token_to_string(TKN_##t);                          \
        return TKN_##t;                                    \
    } while(0)

#define STRING_TOKEN(t)                                    \
    do {                                                   \
        debug_append(#t, yytext);                          \
        save_string(yytext);                               \
        return TKN_##t;                                    \
    } while(0)


#define YY_USER_ACTION                                     \
    {                                                      \
        yy_videoep_column.first = yy_videoep_column.last;  \
        yy_videoep_column.last += yy_videoep_leng;         \
    }

static inline int yywrap(void)
{
    return 1;
}


YYSTYPE   yy_videoep_lval;
yy_column yy_videoep_column;

static char  dbgbuf[4096];
static char *dbgend = dbgbuf + sizeof(dbgbuf);
static char *dbgptr = dbgbuf;

static void  token_to_string(int);
static void  save_string(const char *);
static void  debug_append(const char *, const char *);
static void  debug_flush(void);

%}


EOL                   \n
WHITESPACE            [ \t]+
CONTINUATION          \\\n
COMMENT               #.*$


FUNCTION              function
SEQUENCE              sequence
RESOLVER              resolver

NAME                  name
STEP_UNTIL            step-until
STEP                  step
FIRST_SUCCESS         first-success
FIRST_FAILURE         first-failure
END                   end

ROOT_WINDOW           root-window
ROOT_WINDOW_ID        root-window-id
ROOT_PROPERTY         root-property
NEW_WINDOW            new-window
APP_WINDOW            application-window
APP_WINDOW_ID         application-window-id
APP_PROPERTY          application-property
WIN_PROPERTY          window-property
WIN_ID                window-id
VARIABLE              variable
ATOM                  atom
PROPERTY              property
WINDOW                window
CARDINAL              cardinal
STRING                string
INTEGER               integer
UNSIGNED              unsigned
IF                    if
THEN                  then
ELSE                  else
ENDIF                 endif

POSITIVE_NUMBER       \+[0-9]+
NEGATIVE_NUMBER       \-[0-9]+
UNSIGNED_NUMBER       [0-9]+
IDENTIFIER            [a-zA-Z_]([a-zA-Z0-9_-]*[a-zA-Z0-9])*
TEXT                  (('[^\n']*')|(\"[^\n\"]*\"))

ASSIGN                =
COMMA                 ,
COLON                 :
IMPLICATION           =>
OPEN_PARNETHESIS      \(
CLOSE_PARENTHESIS     \)
OPEN_BRACKET          \[
CLOSE_BRACKET         \]
LESS                  <
LESS_OR_EQUAL         <=
EQUAL                 ==
GREATER_OR_EQUAL      >=
GREATER               >
PLUS                  \+
MINUS                 \-
ASTERISK              \*
SLASH                 \/


%%

{EOL}                   { EOL_TOKEN;          }
<<EOF>>                 { EOF_TOKEN;          }
{WHITESPACE}            { /* ignore */        }
{CONTINUATION}          { CONTINUATION_TOKEN; }
{COMMENT}               { /* ignore */        }

{FUNCTION}              { VALUELESS_TOKEN (FUNCTION);          }
{SEQUENCE}              { VALUELESS_TOKEN (SEQUENCE);          }
{RESOLVER}              { VALUELESS_TOKEN (RESOLVER);          }

{NAME}                  { VALUELESS_TOKEN (NAME);              }
{STEP_UNTIL}            { VALUELESS_TOKEN (STEP_UNTIL);        }
{STEP}                  { VALUELESS_TOKEN (STEP);              }
{FIRST_SUCCESS}         { VALUELESS_TOKEN (FIRST_SUCCESS);     }
{FIRST_FAILURE}         { VALUELESS_TOKEN (FIRST_FAILURE);     }
{END}                   { VALUELESS_TOKEN (END);               }

{ROOT_WINDOW}           { VALUELESS_TOKEN (ROOT_WINDOW);       }
{ROOT_WINDOW_ID}        { VALUELESS_TOKEN (ROOT_WINDOW_ID);    }
{ROOT_PROPERTY}         { VALUELESS_TOKEN (ROOT_PROPERTY);     }
{NEW_WINDOW}            { VALUELESS_TOKEN (NEW_WINDOW);        }
{APP_WINDOW}            { VALUELESS_TOKEN (APP_WINDOW);        }
{APP_WINDOW_ID}         { VALUELESS_TOKEN (APP_WINDOW_ID);     }
{APP_PROPERTY}          { VALUELESS_TOKEN (APP_PROPERTY);      }
{WIN_PROPERTY}          { VALUELESS_TOKEN (WIN_PROPERTY);      }
{WIN_ID}                { VALUELESS_TOKEN (WIN_ID);            }
{VARIABLE}              { VALUELESS_TOKEN (VARIABLE);          }
{ATOM}                  { VALUELESS_TOKEN (ATOM);              }
{PROPERTY}              { VALUELESS_TOKEN (PROPERTY);          }
{WINDOW}                { VALUELESS_TOKEN (WINDOW);            }
{CARDINAL}              { VALUELESS_TOKEN (CARDINAL);          }
{STRING}                { VALUELESS_TOKEN (STRING);            }
{INTEGER}               { VALUELESS_TOKEN (INTEGER);           }
{UNSIGNED}              { VALUELESS_TOKEN (UNSIGNED);          }
{IF}                    { VALUELESS_TOKEN (IF);                }
{THEN}                  { VALUELESS_TOKEN (THEN);              }
{ELSE}                  { VALUELESS_TOKEN (ELSE);              }
{ENDIF}                 { VALUELESS_TOKEN (ENDIF);             }

{UNSIGNED_NUMBER}       { STRING_TOKEN    (UNSIGNED_NUMBER);   }
{POSITIVE_NUMBER}       { STRING_TOKEN    (POSITIVE_NUMBER);   }
{NEGATIVE_NUMBER}       { STRING_TOKEN    (NEGATIVE_NUMBER);   }
{IDENTIFIER}            { STRING_TOKEN    (IDENTIFIER);        }
{TEXT}                  { STRING_TOKEN    (TEXT);              }

{ASSIGN}                { VALUELESS_TOKEN (ASSIGN);            }
{COMMA}                 { VALUELESS_TOKEN (COMMA);             }
{COLON}                 { VALUELESS_TOKEN (COLON);             }
{IMPLICATION}           { VALUELESS_TOKEN (IMPLICATION);       }
{OPEN_PARNETHESIS}      { VALUELESS_TOKEN (OPEN_PARENTHESIS);  }
{CLOSE_PARENTHESIS}     { VALUELESS_TOKEN (CLOSE_PARENTHESIS); }
{OPEN_BRACKET}          { VALUELESS_TOKEN (OPEN_BRACKET);      }
{CLOSE_BRACKET}         { VALUELESS_TOKEN (CLOSE_BRACKET);     }
{LESS}                  { VALUELESS_TOKEN (LESS);              }
{LESS_OR_EQUAL}         { VALUELESS_TOKEN (LESS_OR_EQUAL);     }
{EQUAL}                 { VALUELESS_TOKEN (EQUAL);             }
{GREATER_OR_EQUAL}      { VALUELESS_TOKEN (GREATER_OR_EQUAL);  }
{GREATER}               { VALUELESS_TOKEN (GREATER);           }
{PLUS}                  { VALUELESS_TOKEN (PLUS);              }
{MINUS}                 { VALUELESS_TOKEN (MINUS);             }
{ASTERISK}              { VALUELESS_TOKEN (ASTERISK);          }
{SLASH}                 { VALUELESS_TOKEN (SLASH);             }


%%

/*****************************************************
 *
 *  scanner code
 *
 *****************************************************/

static void token_to_string(int token)
{
    static char *string;

    switch (token) {
    case TKN_EOL:                 string = "<eol>";                     break;
    case TKN_FUNCTION:            string = "function";                  break;
    case TKN_SEQUENCE:            string = "sequence";                  break;
    case TKN_RESOLVER:            string = "resolver";                  break;
    case TKN_NAME:                string = "name";                      break;
    case TKN_STEP_UNTIL:          string = "step-until";                break;
    case TKN_STEP:                string = "step";                      break;
    case TKN_FIRST_SUCCESS:       string = "first-success";             break;
    case TKN_FIRST_FAILURE:       string = "first-failure";             break;
    case TKN_END:                 string = "end";                       break;
    case TKN_ROOT_WINDOW:         string = "root-window";               break;
    case TKN_ROOT_WINDOW_ID:      string = "root-window-id";            break;
    case TKN_ROOT_PROPERTY:       string = "root-property";             break;
    case TKN_NEW_WINDOW:          string = "new-window";                break;
    case TKN_APP_WINDOW:          string = "application-window";        break;
    case TKN_APP_WINDOW_ID:       string = "application-window-id";     break;
    case TKN_APP_PROPERTY:        string = "application-property";      break;
    case TKN_WIN_PROPERTY:        string = "window-property";           break;
    case TKN_WIN_ID:              string = "window-id";                 break;
    case TKN_VARIABLE:            string = "variable";                  break;
    case TKN_ATOM:                string = "atom";                      break;
    case TKN_PROPERTY:            string = "property";                  break;
    case TKN_WINDOW:              string = "window";                    break;
    case TKN_CARDINAL:            string = "cardinal";                  break;
    case TKN_STRING:              string = "string";                    break;
    case TKN_INTEGER:             string = "integer";                   break;
    case TKN_UNSIGNED:            string = "unsigned";                  break;
    case TKN_IF:                  string = "if";                        break;
    case TKN_THEN:                string = "then";                      break;
    case TKN_ELSE:                string = "else";                      break;
    case TKN_ENDIF:               string = "endif";                     break;
    case TKN_ASSIGN:              string = "=";                         break;
    case TKN_COMMA:               string = ",";                         break;
    case TKN_COLON:               string = ":";                         break;
    case TKN_IMPLICATION:         string = "=>";                        break;
    case TKN_OPEN_PARENTHESIS:    string = "(";                         break;
    case TKN_CLOSE_PARENTHESIS:   string = ")";                         break;
    case TKN_OPEN_BRACKET:        string = "[";                         break;
    case TKN_CLOSE_BRACKET:       string = "]";                         break;
    case TKN_LESS:                string = "<";                         break;
    case TKN_LESS_OR_EQUAL:       string = "<=";                        break;
    case TKN_EQUAL:               string = "==";                        break;
    case TKN_GREATER_OR_EQUAL:    string = ">=";                        break;
    case TKN_GREATER:             string = ">";                         break;
    case TKN_PLUS:                string = "+";                         break;
    case TKN_MINUS:               string = "-";                         break;
    case TKN_ASTERISK:            string = "*";                         break;
    case TKN_SLASH:               string = "/";                         break;
    default:                      string = "";                          break;
    }

    yy_videoep_lval.string = string;
}

static void save_string(const char *string)
{
    static char  ringbuf[256];
    static char *bufptr;

    const char  *src;
    char        *quoted;

    for (;;) {
        if (bufptr < ringbuf || bufptr >= ringbuf + sizeof(ringbuf))
            bufptr = ringbuf;

        yy_videoep_lval.string = bufptr;
        src = string;

        switch (src[0]) {
        case '\'':   quoted = '\'';   src++;  break;
        case '\"':   quoted = '\"';   src++;  break;
        default:     quoted = 0xff;           break;
        }

        while (bufptr < ringbuf + sizeof(ringbuf)) {
            if ((*bufptr++ = *src++) == '\0') {
                if (bufptr[-2] == quoted)
                    bufptr[-2] = '\0';
                return;
            }
        }
    }
}

static void debug_append(const char *token, const char *value)
{
    if (!DBG_SCAN)
        return;

    if (dbgptr < dbgend) {
        if (value == NULL)
            dbgptr += snprintf(dbgptr, dbgend-dbgptr, ":%s", token); 
        else
            dbgptr += snprintf(dbgptr, dbgend-dbgptr, ":%s[%s]", token, value);
    }
}

static void debug_flush(void)
{
    if (dbgptr > dbgbuf) {
        OHM_DEBUG(DBG_SCAN, "line %d: %s\n", yy_videoep_lineno,
                  dbgbuf + ((dbgbuf[0] == ':') ? 1:0));
        dbgptr = dbgbuf;
        dbgptr[0] = '\0';
    }
}

int scanner_open_file(const char *path)
{
    if (!path || !(yyin = fopen(path, "r")))
        return -1;

    return 0;
}





/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
