/*****************************************************************************/
/*  Copyright (C) 2010 Nokia Corporation.                                    */
/*                                                                           */
/*  These OHM Modules are free software; you can redistribute                */
/*  it and/or modify it under the terms of the GNU Lesser General Public     */
/*  License as published by the Free Software Foundation                     */
/*  version 2.1 of the License.                                              */
/*                                                                           */
/*  This library is distributed in the hope that it will be useful,          */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of           */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU         */
/*  Lesser General Public License for more details.                          */
/*                                                                           */
/*  You should have received a copy of the GNU Lesser General Public         */
/*  License along with this library; if not, write to the Free Software      */
/*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 */
/*  USA.                                                                     */
/*****************************************************************************/

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
#include "randr.h"
#include "router.h"


extern int                       yy_videoep_lineno;

static const char               *yydeffile;

static randr_mode_def_t          modef;
static struct router_sequence_s *rtdef;
static config_scrdef_t           scrdef;
static char                     *output;
static config_propdef_t          propdef;

static sequence_def_t            seqdef;

static config_windef_t           windefs[128];
static int                       winidx;

static exec_def_t                execdefs[64];
static int                       execidx;

static argument_def_t            argdefs[256];
static argument_def_t           *argstart = argdefs;
static int                       argidx;

static int                       newwin_section_present;
static int                       appwin_section_present;

static int                       position;


static int  unsigned_number(const char *, uint32_t *);
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
%token <string> TKN_DEVICE
%token <string> TKN_OUT_SIGNAL
%token <string> TKN_OUT_RATIO
%token <string> TKN_TYPE
%token <string> TKN_SCREEN
%token <string> TKN_CRTC
%token <string> TKN_ROOT_WINDOW
%token <string> TKN_ROOT_WINDOW_ID
%token <string> TKN_ROOT_PROPERTY
%token <string> TKN_NEW_WINDOW
%token <string> TKN_APP_WINDOW
%token <string> TKN_APP_WINDOW_ID
%token <string> TKN_APP_PROPERTY
%token <string> TKN_WIN_PROPERTY
%token <string> TKN_OUT_PROPERTY
%token <string> TKN_XV_PROPERTY
%token <string> TKN_WIN_ID
%token <string> TKN_VARIABLE
%token <string> TKN_ATOM
%token <string> TKN_PROPERTY
%token <string> TKN_WINDOW
%token <string> TKN_OUTPUT
%token <string> TKN_INPUT
%token <string> TKN_VIDEO
%token <string> TKN_TARGET
%token <string> TKN_MODE
%token <string> TKN_POSITION
%token <string> TKN_DONTCARE
%token <string> TKN_APPEND
%token <string> TKN_HSYNC_POSITIVE
%token <string> TKN_HSYNC_NEGATIVE
%token <string> TKN_VSYNC_POSITIVE
%token <string> TKN_VSYNC_NEGATIVE
%token <string> TKN_INTERLACE
%token <string> TKN_DOUBLE_SCAN
%token <string> TKN_CSYNC
%token <string> TKN_CSYNC_POSITIVE
%token <string> TKN_CSYNC_NEGATIVE
%token <string> TKN_PIXEL_MULTIPLEX
%token <string> TKN_DOUBLE_CLOCK
%token <string> TKN_HALVE_CLOCK
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
%token <string> TKN_LOGICAL_OR
%token <string> TKN_LOGICAL_AND

%token <string> TKN_UNSIGNED_NUMBER
%token <string> TKN_POSITIVE_NUMBER
%token <string> TKN_NEGATIVE_NUMBER
%token <string> TKN_IDENTIFIER
%token <string> TKN_TEXT

%type <string>  position_value
%type <string>  id_value
%type <string>  atom_value
%type <seqtype> sequence_type
%type <execdef> sequence_step_definition
%type <valtyp>  window_property_type
%type <valtyp>  output_property_type
%type <valtyp>  xvideo_property_type
%type <windef>  window_property_exec_definition
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
  TKN_IDENTIFIER TKN_ASSIGN atom_value TKN_EOL {
      OHM_DEBUG(DBG_PARSE, "atom_create(%s, %s)\n", $1, $3);
      tracker_add_atom($1, $3);
      position = 0;
  }
| invalid_line
;

atom_value:
  TKN_IDENTIFIER  { $$ = $1; }
| TKN_TEXT        { $$ = $1; }
;

property_section: TKN_OPEN_BRACKET property_section_type;

property_section_type:
  obsolete_property_section window_property_definitions
| window_property_section window_property_definitions
| output_property_section output_property_definitions
| xvideo_property_section xvideo_property_definitions
;

obsolete_property_section:
  TKN_PROPERTY TKN_CLOSE_BRACKET TKN_EOL {
      position = 0;
  }
;

window_property_section:
  TKN_WIN_PROPERTY TKN_CLOSE_BRACKET TKN_EOL {
      position = 0;
  }
;

window_property_definitions:
  window_property_definition
| window_property_definitions window_property_definition
;

window_property_definition:
  TKN_IDENTIFIER TKN_ASSIGN
      window_property_type TKN_COMMA TKN_IDENTIFIER TKN_EOL
  {
      OHM_DEBUG(DBG_PARSE, "atom_create(%s, %s)\n", $1, $5);
      atom_create($1, $5);

      OHM_DEBUG(DBG_PARSE, "property_definition_create(%s, %d)\n",$1,$3);
      property_definition_create($1, $3);

      position = 0;
  }
| invalid_line
;

window_property_type:
  TKN_ATOM      { $$ = videoep_atom;   }
| TKN_CARDINAL  { $$ = videoep_card;   }
| TKN_STRING    { $$ = videoep_string; }
| TKN_WINDOW    { $$ = videoep_window; }
;

output_property_section:
  TKN_OUT_PROPERTY TKN_CLOSE_BRACKET TKN_EOL {
      position = 0;
  }
;

output_property_definitions:
  output_property_definition
| output_property_definitions output_property_definition
;

output_property_definition:
  TKN_IDENTIFIER TKN_COLON TKN_IDENTIFIER TKN_ASSIGN
      output_property_type TKN_COMMA TKN_IDENTIFIER TKN_EOL
  {
      OHM_DEBUG(DBG_PARSE, "atom_create(%s, %s)\n", $3, $7);
      atom_create($3, $7);

      OHM_DEBUG(DBG_PARSE, "randr_output_define_property(%s, %s, %s, %d)",
                $1,$3, $7, $5);
      randr_output_define_property($1, $3, $7, $5);

      position = 0;
  }
| invalid_line
;

output_property_type:
  TKN_ATOM      { $$ = videoep_atom;   }
| TKN_CARDINAL  { $$ = videoep_card;   }
| TKN_STRING    { $$ = videoep_string; }
;

xvideo_property_section:
  TKN_XV_PROPERTY TKN_CLOSE_BRACKET TKN_EOL {
      position = 0;
  }
;

xvideo_property_definitions:
  xvideo_property_definition
| xvideo_property_definitions xvideo_property_definition
;

xvideo_property_definition:
  TKN_IDENTIFIER TKN_COLON TKN_IDENTIFIER TKN_ASSIGN
      window_property_type TKN_COMMA TKN_IDENTIFIER TKN_EOL
  {
      OHM_DEBUG(DBG_PARSE, "atom_create(%s, %s)\n", $3, $7);
      atom_create($3, $7);

#if 0
      OHM_DEBUG(DBG_PARSE, "xvideo_adaptor_define_property(%s, %s %s, %d)\n",
                $1,$3, $7, $5);
      xvideo_adaptor_define_property($1, $3, $7, $5);
#endif

      position = 0;
  }
| invalid_line
;

xvideo_property_type:
  TKN_ATOM      { $$ = videoep_atom;   }
| TKN_CARDINAL  { $$ = videoep_card;   }
| TKN_STRING    { $$ = videoep_string; }
;


mode_section:
  mode_section_header mode_definitions
;

mode_section_header:
  TKN_OPEN_BRACKET TKN_MODE TKN_CLOSE_BRACKET TKN_EOL {
      position = 0;
  }
;

mode_definitions:
  mode_definition
| mode_definitions mode_definition
;

mode_definition:
  mode_definition_mandatory mode_definition_optional TKN_EOL {

      OHM_DEBUG(DBG_PARSE, "randr_mode_create(<"
                "screen_id:%u, name:'%s', size:%ux%u, "
                "hstart:%u hend:%u, htotal:%u, "
                "vstart:%u vend:%u, vtotal:%u, "
                "hskew:%u, flags:0x%x>)",
                modef.screen_id, modef.name, modef.width,modef.height,
                modef.hstart, modef.hend, modef.htotal,
                modef.vstart, modef.vend, modef.vtotal,
                modef.hskew, modef.flags);

      randr_mode_create(&modef);
  }
;

mode_definition_mandatory:
  id_value TKN_ASSIGN TKN_UNSIGNED_NUMBER TKN_COMMA
    TKN_UNSIGNED_NUMBER TKN_COMMA TKN_UNSIGNED_NUMBER
  {
      memset(&modef, 0, sizeof(modef));
      modef.name = $1;

      if (!unsigned_number($3, &modef.screen_id)) {
          print_error("invalid screen ID '%s'", $3);
          YYERROR;
      }

      if (!unsigned_number($5, &modef.width)  || !modef.width  ||
          !unsigned_number($7, &modef.height) || !modef.height   )
      {
          print_error("invalide mode size '%s,%s'", $5, $7);
          YYERROR;
      }
      
      modef.hstart = modef.hend = modef.htotal = modef.width;
      modef.vstart = modef.vend = modef.vtotal = modef.height;
  }
;

mode_definition_optional:
  /* void */
| mode_clock mode_hsync mode_vsync mode_flags
;

mode_clock:
  TKN_COMMA TKN_UNSIGNED_NUMBER {
      if (!unsigned_number($2, &modef.clock) || modef.clock < 1000000) {
          print_error("invalid mode clock '%s' Hz (must be in MHz range)",$2);
          YYERROR;
      }
  }
;

mode_hsync:
  TKN_COMMA TKN_UNSIGNED_NUMBER /* hstart */
  TKN_COMMA TKN_UNSIGNED_NUMBER /* hend */
  TKN_COMMA TKN_UNSIGNED_NUMBER /* htotal */
  {
      if (!unsigned_number($2, &modef.hstart) ||
          !unsigned_number($4, &modef.hend  ) ||
          !unsigned_number($6, &modef.htotal)   )
      {
          print_error("invalid hstart,hend,htotal values '%s,%s,%s'",$2,$4,$6);
          YYERROR;
      } 
  }
;

mode_vsync:
  TKN_COMMA TKN_UNSIGNED_NUMBER /* vstart */
  TKN_COMMA TKN_UNSIGNED_NUMBER /* vend */
  TKN_COMMA TKN_UNSIGNED_NUMBER /* vtotal */
  {
      if (!unsigned_number($2, &modef.vstart) ||
          !unsigned_number($4, &modef.vend  ) ||
          !unsigned_number($6, &modef.vtotal)   )
      {
          print_error("invalid vstart,vend,vtotal values '%s,%s,%s'",$2,$4,$6);
          YYERROR;
      } 
  }
;

mode_flags:
  /* no flags */
| TKN_COMMA mode_flag
| mode_flags TKN_LOGICAL_OR mode_flag
;

mode_flag:
  TKN_HSYNC_POSITIVE         { modef.flags |= 1;    }
| TKN_HSYNC_NEGATIVE         { modef.flags |= 2;    }
| TKN_VSYNC_POSITIVE         { modef.flags |= 4;    }
| TKN_VSYNC_NEGATIVE         { modef.flags |= 8;    }
| TKN_INTERLACE              { modef.flags |= 16;   }
| TKN_DOUBLE_SCAN            { modef.flags |= 32;   }
| TKN_CSYNC                  { modef.flags |= 64;   }
| TKN_CSYNC_POSITIVE         { modef.flags |= 128;  }
| TKN_CSYNC_NEGATIVE         { modef.flags |= 256;  }
| TKN_PIXEL_MULTIPLEX        { modef.flags |= 2048; }
| TKN_DOUBLE_CLOCK           { modef.flags |= 4096; }
| TKN_HALVE_CLOCK            { modef.flags |= 8192; }
;

device_section:
  device_section_header device_definitions
;

device_section_header:
  TKN_OPEN_BRACKET TKN_DEVICE TKN_CLOSE_BRACKET TKN_EOL
  TKN_TYPE TKN_ASSIGN TKN_IDENTIFIER TKN_EOL {
      if ((rtdef = router_sequence_create(router_seq_device, $7)) == NULL) {
          print_error("multiple device section definition of '%s'", $7);
          YYERROR;
      }
      OHM_DEBUG(DBG_PARSE, "router_sequence_create(device, '%s')", $7);
      position = 0;
  }
;

device_definitions:
  screen_definition
| device_definitions screen_definition
;

screen_definition:
  screen_number crtc_definitions
;

screen_number:
  TKN_SCREEN TKN_ASSIGN TKN_UNSIGNED_NUMBER TKN_EOL {
      memset(&scrdef, 0, sizeof(scrdef));
      scrdef.screen = $3;
  }
;

crtc_definitions:
  crtc_definition
| crtc_definitions crtc_definition
;

crtc_definition:
  crtc_number position_definition mode_definition output_definition {
      int sts;
 
      OHM_DEBUG(DBG_PARSE, "router_add_function(crtc_set_position, %s, %s, "
                "%s, %s)", scrdef.screen, scrdef.crtc, scrdef.x, scrdef.y);

      sts = router_sequence_add_function(rtdef, "crtc_set_position",
                                         scrdef.screen, scrdef.crtc,
                                         scrdef.x, scrdef.y);

      OHM_DEBUG(DBG_PARSE, "router_add_function(crtc_set_mode, %s, %s, %s)",
                scrdef.screen, scrdef.crtc, scrdef.mode?scrdef.mode:"<null>");

      sts = router_sequence_add_function(rtdef, "crtc_set_mode",
                                         scrdef.screen, scrdef.crtc,
                                         scrdef.mode);
                                         
      OHM_DEBUG(DBG_PARSE, "router_add_function(crtc_set_outputs, %s, %s, "
                "%d, <%s ...>)", scrdef.screen, scrdef.crtc, scrdef.outputidx,
                scrdef.outputs[0] ? scrdef.outputs[0] : "NULL");

      sts = router_sequence_add_function(rtdef, "crtc_set_outputs",
                                         scrdef.screen, scrdef.crtc,
                                         scrdef.outputidx, scrdef.outputs);

  }
;

crtc_number:
  TKN_CRTC TKN_ASSIGN TKN_UNSIGNED_NUMBER TKN_EOL {
      scrdef.crtc = $3;
      scrdef.outputidx = 0;
  }
;

position_definition:
  /* void */ {
    scrdef.x = "dontcare";
    scrdef.y = "dontcare";
  }
| TKN_POSITION TKN_ASSIGN position_value TKN_COMMA position_value TKN_EOL {
    scrdef.x = $3;
    scrdef.y = $5;
  }
;

position_value:
  TKN_UNSIGNED_NUMBER      { $$ = $1;         }
| TKN_DONTCARE             { $$ = "dontcare"; }
| TKN_APPEND               { $$ = "append";   }
;

mode_definition:
  /* void */                             { scrdef.mode = NULL; }
| TKN_MODE TKN_ASSIGN id_value TKN_EOL   { scrdef.mode = $3;   }
;

output_definition:
  /* void */
| TKN_OUTPUT TKN_ASSIGN output_list
;

output_list:
  id_value                         { scrdef.outputs[scrdef.outputidx++] = $1; }
| output_list TKN_COMMA id_value   { scrdef.outputs[scrdef.outputidx++] = $3; }
;


signal_section:
  signal_section_header output_definitions
;

signal_section_header:
  TKN_OPEN_BRACKET TKN_OUT_SIGNAL TKN_CLOSE_BRACKET TKN_EOL
  TKN_TYPE TKN_ASSIGN TKN_IDENTIFIER TKN_EOL {
      if ((rtdef = router_sequence_create(router_seq_signal, $7)) == NULL) {
          print_error("multiple signal section definition of '%s'", $7);
          YYERROR;
      }
      OHM_DEBUG(DBG_PARSE, "router_sequence_create(signal, '%s')", $7);
      position = 0;      
  }
;

ratio_section:
  ratio_section_header output_definitions
;

ratio_section_header:
  TKN_OPEN_BRACKET TKN_OUT_RATIO TKN_CLOSE_BRACKET TKN_EOL
  TKN_TYPE TKN_ASSIGN TKN_IDENTIFIER TKN_EOL {
      if ((rtdef = router_sequence_create(router_seq_ratio, $7)) == NULL) {
          print_error("multiple ratio section definition of '%s'", $7);
          YYERROR;
      }
      OHM_DEBUG(DBG_PARSE, "router_sequence_create(ratio, '%s')", $7);
      position = 0;      
  }
;

output_definitions:
  outprop_definition
| output_definitions outprop_definition
;

outprop_definition:
  output_name outprop_settings
;

output_name:
  TKN_OUTPUT TKN_ASSIGN id_value TKN_EOL {
      output = $3;
  }
;

outprop_settings:
  outprop_setting
| outprop_settings outprop_setting
;

outprop_setting:
  TKN_PROPERTY TKN_ASSIGN TKN_IDENTIFIER TKN_COLON id_value TKN_EOL {
      videoep_value_type_t vt = randr_output_get_property_type($3);
      int sts;

      if (vt == videoep_unknown) {
          print_error("attempt to set undefined output property '%s'", $3);
          YYERROR;
      }
      else {
          OHM_DEBUG(DBG_PARSE, "router_add_function(output_change_property, "
                    "'%s', %s, %d, '%s')", output, $3, vt, $5);
          sts = router_sequence_add_function(rtdef, "output_change_property",
                                             output, $3, vt, $5);
      }
  }
;


id_value:
  TKN_IDENTIFIER          { $$ = $1; }
| TKN_TEXT                { $$ = $1; }
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
  window_property_exec_definition TKN_EOL {
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
  window_property_exec_definition TKN_EOL {
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
  window_property_exec_definition TKN_EOL {
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

window_property_exec_definition:
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
| mode_section
| device_section
| signal_section
| ratio_section
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
    ENTER;

    yydeffile = ohm_plugin_get_param(plugin, "config");

    LEAVE;
}

void config_exit(OhmPlugin *plugin)
{
    (void)plugin;
}

int config_parse_file(const char *path)
{
    ENTER;

    if (path == NULL)
        path = yydeffile;

    if (!path) {
        OHM_ERROR("videoep: no configuration file");
        LEAVE;
        return -1;
    }

    if (scanner_open_file(path) < 0) {
        OHM_ERROR("videoep: can't open config file '%s': %s",
                  path, strerror(errno));
        LEAVE;
        return -1;
    }

    OHM_INFO("videoep: configuration file is '%s'", path);

    if (yy_videoep_parse() != 0) {
        OHM_ERROR("videoep: failed to parse config file '%s'", path);
        LEAVE;
        return -1;
    }

    LEAVE;

    return 0;
}

void yy_videoep_error(const char *msg)
{
    OHM_ERROR("videoep: config file parse error in line %d: %s\n",
              yy_videoep_lineno, msg);
}

static int unsigned_number(const char *str, uint32_t *retval)
{
    unsigned long int number;
    char *e;

    *retval = 0;

    if (str != NULL) {
        number = strtoul(str, &e, 10);

        if (e > str && !*e) {
            *retval = number;
            return TRUE;
        }
    }

    return FALSE;
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
