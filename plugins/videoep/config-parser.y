%{

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "plugin.h"
#include "config-private.h"
#include "atom.h"
#include "property.h"

extern int                yy_videoep_lineno;

static const char        *yydeffile;

static sequence_def_t     seqdef;

static config_windef_t    windefs[128];
static int                winidx;

static exec_def_t         execdefs[64];
static int                execidx;

static argument_def_t     argdefs[256];
static argument_def_t    *argstart = argdefs;
static int                argidx;

static int                newwin_section_present;
static int                appwin_section_present;

static int                position;


static void print_error(const char *, ...);

%}


%union {
    char                 *string;
    int32_t               integer;
    uint32_t              unsignd;
    videoep_value_type_t  valtyp;
    config_windef_t      *windef;
    exec_def_t           *execdef;
    argument_def_t       *argdef;
    int                   index;
    sequence_type_t       seqtype;
}

%defines

%token <string> TKN_EOL
%token <string> TKN_FUNCTION
%token <string> TKN_SEQUENCE
%token <string> TKN_RESOLVER
%token <string> TKN_NAME
%token <string> TKN_STEP_UNTIL
%token <string> TKN_STEP
%token <string> TKN_FIRST_SUCCESS
%token <string> TKN_FIRST_FAILURE
%token <string> TKN_END
%token <string> TKN_ROOT_WINDOW
%token <string> TKN_ROOT_WINDOW_ID
%token <string> TKN_ROOT_PROPERTY
%token <string> TKN_NEW_WINDOW
%token <string> TKN_APP_WINDOW
%token <string> TKN_APP_WINDOW_ID
%token <string> TKN_APP_PROPERTY
%token <string> TKN_WIN_PROPERTY
%token <string> TKN_WIN_ID
%token <string> TKN_VARIABLE
%token <string> TKN_ATOM
%token <string> TKN_PROPERTY
%token <string> TKN_WINDOW
%token <string> TKN_CARDINAL
%token <string> TKN_STRING
%token <string> TKN_INTEGER
%token <string> TKN_UNSIGNED
%token <string> TKN_IF
%token <string> TKN_THEN
%token <string> TKN_ELSE
%token <string> TKN_ENDIF
%token <string> TKN_ASSIGN
%token <string> TKN_COMMA
%token <string> TKN_COLON
%token <string> TKN_IMPLICATION
%token <string> TKN_OPEN_PARENTHESIS
%token <string> TKN_CLOSE_PARENTHESIS
%token <string> TKN_OPEN_BRACKET
%token <string> TKN_CLOSE_BRACKET
%token <string> TKN_LESS
%token <string> TKN_LESS_OR_EQUAL
%token <string> TKN_EQUAL
%token <string> TKN_GREATER_OR_EQUAL
%token <string> TKN_GREATER
%token <string> TKN_PLUS
%token <string> TKN_MINUS
%token <string> TKN_ASTERISK
%token <string> TKN_SLASH

%token <string> TKN_UNSIGNED_NUMBER
%token <string> TKN_POSITIVE_NUMBER
%token <string> TKN_NEGATIVE_NUMBER
%token <string> TKN_IDENTIFIER
%token <string> TKN_TEXT

%type <seqtype> sequence_type
%type <execdef> sequence_step_definition
%type <valtyp>  property_value_type
%type <windef>  window_property_definition
%type <string>  window_property
%type <string>  window_property_id_assingnment
%type <execdef> executable_definition
%type <execdef> executable
%type <argdef>  value_definition
%type <integer> index_definition
%type <string>  integer

%start configuration

%%

atom_section:
  atom_section_header atom_definitions
;

atom_section_header:
  TKN_OPEN_BRACKET TKN_ATOM TKN_CLOSE_BRACKET TKN_EOL {
      position = 0;
  }
;

atom_definitions:
  atom_definition
| atom_definitions atom_definition
;

atom_definition:
  TKN_IDENTIFIER TKN_ASSIGN TKN_IDENTIFIER TKN_EOL {
      OHM_DEBUG(DBG_PARSE, "atom_create(%s, %s)\n", $1, $3);
      tracker_add_atom($1, $3);
      position = 0;
  }
| invalid_line
;

property_section:
  property_section_header property_definitions
;

property_section_header:
  TKN_OPEN_BRACKET TKN_PROPERTY TKN_CLOSE_BRACKET TKN_EOL {
      position = 0;
  }
;

property_definitions:
  property_definition
| property_definitions property_definition
;

property_definition:
 TKN_IDENTIFIER TKN_ASSIGN property_value_type TKN_COMMA TKN_IDENTIFIER TKN_EOL
  {
      OHM_DEBUG(DBG_PARSE, "atom_create(%s, %s)\n", $1, $5);
      atom_create($1, $5);

      OHM_DEBUG(DBG_PARSE, "property_definition_create(%s, %d)\n",$1,$3);
      property_definition_create($1, $3);
      position = 0;
  }
| invalid_line
;

property_value_type:
  TKN_ATOM      { $$ = videoep_atom;   }
| TKN_CARDINAL  { $$ = videoep_card;   }
| TKN_STRING    { $$ = videoep_string; }
| TKN_WINDOW    { $$ = videoep_window; }
;

sequence_section:
  sequence_section_header sequence_base_definition sequence_step_definitions {
      int sts = sequence_definition_create(seqdef.type, seqdef.name,
                                           seqdef.stepc, seqdef.stepd);
      if (sts < 0) {
          print_error("failed to create sequence '%s'",
                      seqdef.name ? seqdef.name : "<null>");
          YYERROR;
      }
  }
;

sequence_section_header:
  TKN_OPEN_BRACKET TKN_SEQUENCE TKN_CLOSE_BRACKET TKN_EOL {
      memset(&seqdef, 0, sizeof(seqdef));
      seqdef.stepd = execdefs;
      execidx  = 0;
      argstart = argdefs;
      argidx   = 0;
      position = 0;
  }
;

sequence_base_definition:
  TKN_NAME TKN_ASSIGN TKN_IDENTIFIER TKN_EOL
  TKN_STEP_UNTIL TKN_ASSIGN sequence_type TKN_EOL {
      seqdef.type = $7;
      seqdef.name = $3;
  }
;

sequence_type:
  TKN_FIRST_SUCCESS   { $$ = sequence_until_first_success; }
| TKN_FIRST_FAILURE   { $$ = sequence_until_first_failure; }
| TKN_END             { $$ = sequence_unconditional;       }
;

sequence_step_definitions:
  sequence_step_definition
| sequence_step_definitions sequence_step_definition
;

sequence_step_definition:
  TKN_STEP TKN_ASSIGN executable_definition TKN_EOL {
      seqdef.stepc = execidx;
  }
;


root_window_section:
  root_window_section_header root_window_definitions
;

root_window_section_header:
  TKN_OPEN_BRACKET TKN_ROOT_WINDOW TKN_CLOSE_BRACKET TKN_EOL {
      winidx = 0;
      execidx = 0;
      position = 0;
  }
;

root_window_definitions:
  root_window_definition
| root_window_definitions root_window_definition
;  

root_window_definition:
  window_property_definition TKN_EOL {
      int sts;

      switch ($1->type) {
      case windef_property:
          if ($1->property.exec == NULL) {
              OHM_DEBUG(DBG_PARSE, "tracker_add_rootwin_property"
                        "(%s, %s,<null>, 0,<null>)", $1->property.name,
                        exec_type_str(exec_noexec));
              sts = tracker_add_rootwin_property($1->property.name,
                                                 exec_noexec,
                                                 NULL, 0,NULL);
          }
          else {
              exec_def_t *exec = $1->property.exec;

              OHM_DEBUG(DBG_PARSE, "tracker_add_rootwin_property"
                        "(%s, %s, %s, %d,%p)", $1->property.name,
                        exec_type_str(exec->type),
                        exec->name, exec->argc, exec->argd);
              sts = tracker_add_rootwin_property($1->property.name,
                                                 exec->type, exec->name,
                                                 exec->argc,exec->argd);
          }
          if (sts < 0) {
              print_error("failed to add root window property");
              YYERROR;
          }
          break;
      default:
          print_error("unsupported executable type");
          YYERROR;
          break;
      }
      position = 0;
  }
| invalid_line
;

new_window_section:
  new_window_section_header new_window_definitions
;

new_window_section_header:
  TKN_OPEN_BRACKET TKN_NEW_WINDOW TKN_CLOSE_BRACKET TKN_EOL {
      newwin_section_present = TRUE;
      winidx = 0;
      execidx = 0;
      position = 0;
  }
;

new_window_definitions:
  new_window_definition
| new_window_definitions new_window_definition
;  

new_window_definition:
  window_property_definition TKN_EOL {
      int sts;

      if (appwin_section_present) {
          print_error("new-window section must preceed "
                      "application-window section");
          YYERROR;
      }
      else {
          switch ($1->type) {
          case windef_property:
              if ($1->property.exec == NULL) {
                  OHM_DEBUG(DBG_PARSE, "tracker_add_newwin_property"
                            "(%s, %s,<null>, 0,<null>)", $1->property.name,
                            exec_type_str(exec_noexec));
                  sts = tracker_add_newwin_property($1->property.name,
                                                    exec_noexec,
                                                    NULL, 0,NULL);
              }
              else {
                  exec_def_t *exec = $1->property.exec;
                  
                  OHM_DEBUG(DBG_PARSE, "tracker_add_newwin_property"
                            "(%s, %s, %s, %d,%p)", $1->property.name,
                            exec_type_str(exec->type),
                            exec->name, exec->argc, exec->argd);
                  sts = tracker_add_newwin_property($1->property.name,
                                                    exec->type, exec->name,
                                                    exec->argc, exec->argd);
              }
              if (sts < 0) {
                  print_error("failed to add new window property");
                  YYERROR;
              }
              break;
          default:
              print_error("unsupported executable type");
              YYERROR;
              break;
          }
      }
      position = 0;
  }
| invalid_line
;



app_window_section:
  app_window_section_header app_window_definitions
;

app_window_section_header:
  TKN_OPEN_BRACKET TKN_APP_WINDOW TKN_CLOSE_BRACKET TKN_EOL {
      appwin_section_present = TRUE;
      winidx = 0;
      execidx = 0;
      position = 0;
  }
;

app_window_definitions:
  app_window_definition
| app_window_definitions app_window_definition
;  

app_window_definition:
  window_property_definition TKN_EOL {
      int sts;

      switch ($1->type) {
      case windef_property:
          if ($1->property.exec == NULL) {
              OHM_DEBUG(DBG_PARSE, "tracker_add_appwin_property"
                        "(%s, %s,<null>, 0,<null>)", $1->property.name,
                        exec_type_str(exec_noexec));
              sts = tracker_add_appwin_property($1->property.name,
                                                exec_noexec,
                                                NULL, 0,NULL);
          }
          else {
              exec_def_t *exec = $1->property.exec;

              OHM_DEBUG(DBG_PARSE, "tracker_add_appwin_property"
                        "(%s, %s, %s, %d,%p)", $1->property.name,
                        exec_type_str(exec->type),
                        exec->name, exec->argc, exec->argd);
              sts = tracker_add_appwin_property($1->property.name,
                                                exec->type, exec->name,
                                                exec->argc, exec->argd);
          }
          if (sts < 0) {
              print_error("failed to add application window property");
              YYERROR;
          }
          break;
      default:
          print_error("unsupported executable type");
          YYERROR;
          break;
      }
      position = 0;
  }
| invalid_line
;

window_property_definition:
  window_property {
      $$ = windefs + winidx++;
      $$->type = windef_property;
      $$->property.name = $1;
      $$->property.exec = NULL;
  }
| window_property TKN_IMPLICATION executable_definition {
      $$ = windefs + winidx++;
      $$->type = windef_property;
      $$->property.name = $1;
      $$->property.exec = $3;
  }
;

window_property:
  TKN_PROPERTY window_property_id_assingnment {
      $$ = $2;
  }
;

window_property_id_assingnment:
  TKN_ASSIGN TKN_IDENTIFIER {
      argstart = argdefs;
      argidx = 0;
      $$ = $2;
  }
;

executable_definition:
  TKN_FUNCTION TKN_COLON executable
    TKN_OPEN_PARENTHESIS positional_argument_definitions TKN_CLOSE_PARENTHESIS
  {
      $$ = $3;
      $$->type = exec_function;
      $$->argc = argidx - (argstart -argdefs);
      $$->argd = argstart;
      argstart = argdefs + argidx;
  }
| TKN_SEQUENCE TKN_COLON executable {
      $$ = $3;
      $$->type = exec_sequence;
  }
| TKN_RESOLVER TKN_COLON executable
    TKN_OPEN_PARENTHESIS named_argument_definitions TKN_CLOSE_PARENTHESIS
  {
      $$ = $3;
      $$->type = exec_resolver;
      $$->argc = argidx - (argstart -argdefs);
      $$->argd = argstart;
      argstart = argdefs + argidx;
  }
| TKN_RESOLVER TKN_COLON executable
    TKN_OPEN_PARENTHESIS error TKN_CLOSE_PARENTHESIS
  {
      $$ = $3;
      $$->type = exec_noexec;
      $$->argc = 0;
      $$->argd = NULL;
      print_error("invalid argument near to '%s' at column %d",
                  $<string>5, position);
      yyerrok;
  }
;

executable:
  TKN_IDENTIFIER {
      $$ = execdefs + execidx++;
      $$->type = exec_noexec;
      $$->name = $1;
      $$->argc = 0;
      $$->argd = NULL;
  }
;

positional_argument_definitions:
/* no argument ie. void */
| value_definition
| positional_argument_definitions TKN_COMMA value_definition
;

named_argument_definitions:
/* no argument ie. void */
| named_argument_definition
| named_argument_definitions TKN_COMMA named_argument_definition
;

named_argument_definition:
  TKN_IDENTIFIER TKN_ASSIGN value_definition {
      $3->name = $1; 
  }
;

value_definition:
  TKN_ROOT_WINDOW_ID {
      $$ = argdefs + argidx++;
      $$->type = argument_root_xid;
      $$->name = NULL;
      $$->def  = NULL;
      $$->idx  = 0;
      position = yy_videoep_column.first;
  }
| TKN_APP_WINDOW_ID {
      $$ = argdefs + argidx++;
      $$->type = argument_appwin_xid;
      $$->name = NULL;
      $$->def  = NULL;
      $$->idx  = 0;
      position = yy_videoep_column.first;
  }
| TKN_WIN_ID {
      $$ = argdefs + argidx++;
      $$->type = argument_window_xid;
      $$->name = NULL;
      $$->def  = NULL;
      $$->idx  = 0;
      position = yy_videoep_column.first;
  }
| TKN_ROOT_PROPERTY TKN_COLON  TKN_IDENTIFIER index_definition {
      $$ = argdefs + argidx++;
      $$->type = argument_root_property;
      $$->name = NULL;
      $$->def  = $3;
      $$->idx  = $4; 
      position = yy_videoep_column.first;
  }
| TKN_APP_PROPERTY TKN_COLON TKN_IDENTIFIER index_definition {
      $$ = argdefs + argidx++;
      $$->type = argument_appwin_property;
      $$->name = NULL;
      $$->def  = $3;
      $$->idx  = $4;
      position = yy_videoep_column.first;
  }
| TKN_WIN_PROPERTY TKN_COLON TKN_IDENTIFIER index_definition {
      $$ = argdefs + argidx++;
      $$->type = argument_window_property;
      $$->name = NULL;
      $$->def  = $3;
      $$->idx  = $4;
      position = yy_videoep_column.first;
  }
| TKN_VARIABLE TKN_COLON TKN_IDENTIFIER {
      position = yy_videoep_column.first;
  }
| TKN_ATOM TKN_COLON TKN_IDENTIFIER {
      $$ = argdefs + argidx++;
      $$->type = argument_atom;
      $$->name = NULL;
      $$->def  = $3;
      position = yy_videoep_column.first;
  }
| TKN_STRING TKN_COLON TKN_TEXT {
      $$ = argdefs + argidx++;
      $$->type = argument_constant_string;
      $$->def  = $3;
      position = yy_videoep_column.first;
  }
| TKN_INTEGER TKN_COLON integer {
      $$ = argdefs + argidx++;
      $$->type = argument_constant_integer;
      $$->def = $3;
      position = yy_videoep_column.first;
  }
| TKN_UNSIGNED TKN_COLON TKN_UNSIGNED_NUMBER {
      $$ = argdefs + argidx++;
      $$->type = argument_constant_unsignd;
      $$->def = $3;
      position = yy_videoep_column.first;
  }
;

index_definition:
  /* missing index definition */              { $$ = 0;  }
| TKN_OPEN_BRACKET integer TKN_CLOSE_BRACKET  { $$ = strtol($2,NULL,19); }

integer:
  TKN_UNSIGNED_NUMBER    { $$ = $1; }
| TKN_POSITIVE_NUMBER    { $$ = $1; }
| TKN_NEGATIVE_NUMBER    { $$ = $1; }
;

section:
  atom_section
| property_section
| sequence_section
| root_window_section
| new_window_section
| app_window_section
| TKN_EOL {
      position = 0;
  }
| invalid_line
;

configuration:
  section
| configuration section
;

invalid_line:
  error TKN_EOL { position = 0; yyerrok; }
;

%%

void config_init(OhmPlugin *plugin)
{
    yydeffile = ohm_plugin_get_param(plugin, "config");
}

void config_exit(OhmPlugin *plugin)
{
    (void)plugin;
}

int config_parse_file(const char *path)
{
    if (path == NULL)
        path = yydeffile;

    if (!path) {
        OHM_ERROR("videoep: no configuration file");
        return -1;
    }

    if (scanner_open_file(path) < 0) {
        OHM_ERROR("videoep: can't open config file '%s': %s",
                  path, strerror(errno));
        return -1;
    }

    OHM_INFO("videoep: configuration file is '%s'", path);

    if (yy_videoep_parse() != 0) {
        OHM_ERROR("videoep: failed to parse config file '%s'", path);
        return -1;
    }

    return 0;
}

void yy_videoep_error(const char *msg)
{
    OHM_ERROR("videoep: config file parse error in line %d: %s\n",
              yy_videoep_lineno, msg);
}

static void print_error(const char *fmt, ...)
{
    va_list ap;
    char    buf[512];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    yy_videoep_error(buf);
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
