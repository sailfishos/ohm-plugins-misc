/******************************************************************************/
/*  Copyright (C) 2010 Nokia Corporation.                                     */
/*                                                                            */
/*  These OHM Modules are free software; you can redistribute                 */
/*  it and/or modify it under the terms of the GNU Lesser General Public      */
/*  License as published by the Free Software Foundation                      */
/*  version 2.1 of the License.                                               */
/*                                                                            */
/*  This library is distributed in the hope that it will be useful,           */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU          */
/*  Lesser General Public License for more details.                           */
/*                                                                            */
/*  You should have received a copy of the GNU Lesser General Public          */
/*  License along with this library; if not, write to the Free Software       */
/*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  */
/*  USA.                                                                      */
/******************************************************************************/

%{

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <regex.h>
#include <sched.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#include "cgrp-plugin.h"
#include "cgrp-parser-types.h"
#include "mm.h"  

#define ALL_PRIO "all"
#define LOW_PRIO "lowered"
#define NO_PRIO  "none"

int        cgrpyylex  (void);
void       cgrpyyerror(cgrp_context_t *, const char *);
extern int lexer_start_token;

static int add_gid(cgrp_rule_t *, gid_t);
static int add_grp(cgrp_rule_t *, const char *);
static int lookup_gid(const char *, gid_t *);

static int add_uid(cgrp_rule_t *, uid_t);
static int add_usr(cgrp_rule_t *, const char *);
static int lookup_uid(const char *, uid_t *);

static cgrp_adjust_t parse_adjust(const char *);

static char rule_group[256];

%}

%union {
    token_string_t    any;
    token_string_t    string;
    token_uint32_t    uint32;
    token_sint32_t    sint32;
    token_double_t    dbl;
    cgrp_partition_t  part;
    cgrp_partition_t *parts;
    cgrp_group_t      group;
    cgrp_group_t     *groups;
    cgrp_procdef_t    procdef;
    cgrp_procdef_t   *procdefs;
    cgrp_rule_t      *rules;
    cgrp_rule_t       rule;
    cgrp_follower_t  *follower;
    cgrp_stmt_t      *stmt;
    cgrp_expr_t      *expr;
    cgrp_action_t    *action;
    cgrp_prop_type_t  prop;
    cgrp_value_t      value;
    cgrp_context_t    ctx;
    s64_t             time;
    cgrp_ctrl_setting_t *ctrl_settings;
    cgrp_adjust_t     adjust;
    integer_range_t   int_range;
    double_range_t    dbl_range;
}

%defines
%parse-param {cgrp_context_t *ctx}

%type <part>     partition
%type <part>     partition_properties
%type <string>   partition_path
%type <string>   path
%type <uint32>   partition_cpu_share
%type <uint32>   partition_mem_limit
%type <part>     partition_rt_limit
%type <uint32>   optional_unit
%type <group>    group
%type <group>    group_properties
%type <group>    group_description
%type <group>    group_partition
%type <group>    group_priority
%type <procdef>  procdef
%type <rules>    rules
%type <rules>    rule
%type <rules>    rule_events
%type <rule>     rule_event
%type <string>   procdef_path
%type <stmt>     rule_statements
%type <stmt>     rule_statement
%type <expr>     expr
%type <expr>     bool_expr
%type <expr>     prop_expr
%type <prop>     prop
%type <value>    value
%type <string>   string
%type <time>     time_unit
%type <time>     time_usec
%type <follower> followers
%type <follower> follower
%type <uint32>   schedule_priority
%type <sint32>   renice_priority
%type <adjust>   adjust_action
%type <sint32>   adjust_value
%type <sint32>   integer_value
%type <dbl>      double_value

%type <action> action
%type <action> actions
%type <action> action_group
%type <action> action_classify
%type <action> action_schedule
%type <action> action_renice
%type <action> action_priority
%type <action> action_oom
%type <action> action_ignore
%type <action> action_leads
%type <action> action_no_op

%type <ctrl_settings> optional_cgroup_control_settings
%type <ctrl_settings> cgroup_control_settings
%type <ctrl_settings> cgroup_control_setting
%type <string> cgroup_control_path
%type <string> control_value
%type <string> string_or_wildcard
%type <ctrl_settings> optional_partition_controls
%type <ctrl_settings> partition_controls
%type <ctrl_settings> partition_control
%type <int_range> integer_range
%type <int_range> optional_integer_range
%type <dbl_range> double_range

%token START_FULL_PARSER
%token START_ADDON_PARSER

%token KEYWORD_GLOBAL
%token KEYWORD_PARTITION
%token KEYWORD_DESCRIPTION
%token KEYWORD_PATH
%token KEYWORD_CPU_SHARES
%token KEYWORD_MEM_LIMIT
%token KEYWORD_REALTIME_LIMIT
%token KEYWORD_RULE
%token KEYWORD_BINARY
%token KEYWORD_CMDLINE
%token KEYWORD_NAME
%token KEYWORD_GROUP
%token KEYWORD_RENICE
%token KEYWORD_SCHEDULE
%token KEYWORD_USER
%token KEYWORD_PARENT
%token KEYWORD_TYPE
%token KEYWORD_IGNORE
%token KEYWORD_LEADS
%token KEYWORD_RECLASSIFY
%token KEYWORD_RECLASS_AFTER
%token KEYWORD_CLASSIFY
%token KEYWORD_PRIORITY
%token KEYWORD_OOM
%token KEYWORD_RESPONSE_CURVE
%token KEYWORD_NO_OP
%token KEYWORD_EXPORT_GROUPS
%token KEYWORD_EXPORT_PARTITIONS
%token KEYWORD_EXPORT_FACT
%token KEYWORD_CGROUPFS_OPTIONS
%token KEYWORD_CGROUP_CONTROL
%token KEYWORD_IOWAIT_NOTIFY
%token KEYWORD_IOQLEN_NOTIFY
%token KEYWORD_SWAP_PRESSURE
%token KEYWORD_ADDON_RULES
%token KEYWORD_ALWAYS_FALLBACK
%token KEYWORD_PRESERVE_PRIO

%token TOKEN_EOL "\n"
%token TOKEN_ASTERISK "*"
%token TOKEN_HEADER_OPEN "["
%token TOKEN_HEADER_CLOSE "]"
%token TOKEN_CURLY_OPEN   "{"
%token TOKEN_CURLY_CLOSE  "}"
%token TOKEN_AND       "&&"
%token TOKEN_OR        "||"
%token TOKEN_NOT       "!"
%token TOKEN_EQUAL     "=="
%token TOKEN_NOTEQ     "!="
%token TOKEN_LESS      "<"
%token TOKEN_GREATER   ">"
%token TOKEN_IMPLIES   "=>"
%token TOKEN_SEMICOLON ";"
%token TOKEN_COMMA     ","
%token TOKEN_COLON     ":"

%token <uint32> TOKEN_ARG
%token <uint32> KEYWORD_CLASSIFY_ARGVX
%token <string> TOKEN_IDENT
%token <string> TOKEN_PATH
%token <string> TOKEN_STRING
%token <uint32> TOKEN_UINT
%token <sint32> TOKEN_SINT
%token <dbl>    TOKEN_DOUBLE


%%

configuration: START_FULL_PARSER 
                 global_section partition_section group_section procdef_section
    | START_ADDON_PARSER
                 procdef_section
    ;

/*****************************************************************************
 *                            *** global section ***                         *
 *****************************************************************************/

global_section: /* empty: whole section can be omitted */
    | KEYWORD_GLOBAL "\n" global_options
    ;

global_options: /* empty: allow just the header without any actual options */
    | global_option 
    | global_options global_option
    ;

global_option: KEYWORD_EXPORT_GROUPS "\n" {
	  CGRP_SET_FLAG(ctx->options.flags, CGRP_FLAG_GROUP_FACTS);
    }
    | KEYWORD_EXPORT_PARTITIONS "\n" {
          CGRP_SET_FLAG(ctx->options.flags, CGRP_FLAG_PART_FACTS);
    }
    | KEYWORD_ALWAYS_FALLBACK "\n" {
          CGRP_SET_FLAG(ctx->options.flags, CGRP_FLAG_ALWAYS_FALLBACK);
    }
    | KEYWORD_PRESERVE_PRIO TOKEN_IDENT "\n" {
          char *what = $2.value;
          int   prio;

          if      (!strcmp(what, ALL_PRIO)) prio = CGRP_PRIO_ALL;
          else if (!strcmp(what, LOW_PRIO)) prio = CGRP_PRIO_LOW;
          else if (!strcmp(what, NO_PRIO )) prio = CGRP_PRIO_NONE;
          else {
              OHM_ERROR("cgrp: invalid %s setting '%s'",
                        "preserve-priority", what);
              OHM_ERROR("cgrp: allowed settings are: '%s', '%s', '%s'",
                        ALL_PRIO, LOW_PRIO, NO_PRIO);
              prio = CGRP_PRIO_LOW;
          }
          
          ctx->options.prio_preserve = prio;
    }
    | iowait_notify "\n"
    | ioqlen_notify "\n"
    | swap_pressure "\n"
    | cgroupfs_options "\n"
    | addon_rules "\n"
    | cgroup_control "\n"
    | priority_response_curve "\n"
    | oom_response_curve "\n"
    | error {
        OHM_ERROR("cgrp: failed to parse global options near token '%s'",
                  cgrpyylval.any.token);
        exit(1);
    }
    ;

iowait_notify: KEYWORD_IOWAIT_NOTIFY iowait_notify_options
    ;

iowait_notify_options: iowait_notify_option
    | iowait_notify_options iowait_notify_option
    ;

iowait_notify_option: TOKEN_IDENT TOKEN_UINT TOKEN_UINT {
          if (!strcmp($1.value, "threshold")) {
              ctx->iow.thres_low  = $2.value;
              ctx->iow.thres_high = $3.value;
          }
          else if (!strcmp($1.value, "poll")) {
              ctx->iow.poll_high = $2.value;
              ctx->iow.poll_low  = $3.value;
          }
          else {
              OHM_ERROR("cgrp: invalid iowait-notify parameter %s", $1.value);
	      YYABORT;
          }
    }
    | TOKEN_IDENT TOKEN_UINT {
          if (!strcmp($1.value, "startup-delay"))
              ctx->iow.startup_delay = $2.value;
          else {
              ctx->iow.nsample = $2.value;
              ctx->iow.estim   = estim_alloc($1.value, $2.value);
              if (ctx->iow.estim == NULL)
                  YYABORT;
          }
    }
    | TOKEN_IDENT string {
          if (!strcmp($1.value, "hook"))
              ctx->iow.hook = STRDUP($2.value);
          else {
              OHM_ERROR("cgrp: invalid iowait-notify parameter %s", $1.value);
	      YYABORT;
          }
    }
    | error { 
          OHM_ERROR("cgrp: failed to parse I/O wait options near token '%s'",
                    cgrpyylval.any.token);
          exit(1);
    }
    ;


ioqlen_notify: KEYWORD_IOQLEN_NOTIFY path ioqlen_notify_options {
        ctx->ioq.path = STRDUP($2.value);
    }
    ;

ioqlen_notify_options: ioqlen_notify_option
    | ioqlen_notify_options ioqlen_notify_option
    ;

ioqlen_notify_option: TOKEN_IDENT TOKEN_UINT TOKEN_UINT {
          if (!strcmp($1.value, "threshold")) {
              ctx->ioq.thres_low  = $2.value;
              ctx->ioq.thres_high = $3.value;
          }
          else {
              OHM_ERROR("cgrp: invalid ioqlen-notify parameter %s", $1.value);
	      YYABORT;
          }
    }
    | TOKEN_IDENT TOKEN_UINT {
          if (!strcmp($1.value, "period"))
              ctx->ioq.period = $2.value;
          else {
              OHM_ERROR("cgrp: invalid ioqlen-notify parameter %s", $1.value);
	      YYABORT;
          }
    } 
    | TOKEN_IDENT string {
          if (!strcmp($1.value, "hook"))
              ctx->ioq.hook = STRDUP($2.value);
          else {
              OHM_ERROR("cgrp: invalid ioqlen-notify parameter %s", $1.value);
	      YYABORT;
          }
    }
    | error { 
          OHM_ERROR("cgrp: failed to parse I/O wait options near token '%s'",
                    cgrpyylval.any.token);
          exit(1);
    }

swap_pressure: KEYWORD_SWAP_PRESSURE swap_pressure_options
    ;

swap_pressure_options: swap_pressure_option
    | swap_pressure_options swap_pressure_option
    ;

swap_pressure_option: TOKEN_IDENT TOKEN_UINT TOKEN_UINT {
          if (strcmp($1.value, "threshold")) {
              OHM_ERROR("cgrp: invalid swap-pressure parameter %s", $1.value);
	      YYABORT;
          }
          ctx->swp.low  = $2.value;
          ctx->swp.high = $3.value;
    }
    | TOKEN_IDENT TOKEN_UINT {
          if (!strcmp($1.value, "min-delay"))
              ctx->swp.interval = $2.value;
          else {
              OHM_ERROR("cgrp: invalid swap-pressure parameter %s", $1.value);
	      YYABORT;
          }
    }
    | TOKEN_IDENT string {
          if (!strcmp($1.value, "hook"))
              ctx->swp.hook = STRDUP($2.value);
          else {
              OHM_ERROR("cgrp: invalid swap-pressure parameter %s", $1.value);
	      YYABORT;
          }
    }
    ;

cgroupfs_options: KEYWORD_CGROUPFS_OPTIONS mount_options
    ;

mount_options: TOKEN_IDENT      { cgroup_set_option(ctx, $1.value); }
    | mount_options TOKEN_IDENT { cgroup_set_option(ctx, $2.value); }
    ;

addon_rules: KEYWORD_ADDON_RULES optional_monitor path {
          ctx->options.addon_rules = STRDUP($3.value);
    }
    ;

optional_monitor: /* empty */ {
          CGRP_CLR_FLAG(ctx->options.flags, CGRP_FLAG_ADDON_MONITOR);
    }
    | TOKEN_IDENT {
          if (!strcmp($1.value, "monitor"))
              CGRP_SET_FLAG(ctx->options.flags, CGRP_FLAG_ADDON_MONITOR);
          else
              OHM_ERROR("cgrp: ignoring addon-rule option '%s'", $1.value);
    }
    ;

cgroup_control: KEYWORD_CGROUP_CONTROL cgroup_control_def
    ;

cgroup_control_def: string cgroup_control_path 
                    optional_cgroup_control_settings {
          cgrp_ctrl_t *ctrl, **p;

          if (ALLOC_OBJ(ctrl) == NULL) {
              OHM_ERROR("cgrp: failed to allocate cgroup control");
              exit(1);
          }

          ctrl->name = STRDUP($1.value);
	  ctrl->path = STRDUP($2.value);
          ctrl->settings = $3;

          for (p = &ctx->controls; *p != NULL; p = &(*p)->next)
    	      ;
          *p = ctrl;
    }
    ;

cgroup_control_path: TOKEN_STRING { $$ = $1; }
    ;


optional_cgroup_control_settings: /* empty */ { $$ = NULL; }
    |    cgroup_control_settings              { $$ = $1; }
    ;

cgroup_control_settings: cgroup_control_setting {
          $$ = $1;
    }
    | cgroup_control_settings cgroup_control_setting {
          cgrp_ctrl_setting_t *setting;

          for (setting = $1; setting->next != NULL; setting = setting->next)
              ;
          setting->next = $2;
          $$            = $1;
    }
    ;

cgroup_control_setting: string TOKEN_COLON control_value {
          cgrp_ctrl_setting_t *setting;

          if (ALLOC_OBJ(setting) == NULL) {
              OHM_ERROR("cgrp: failed to allocate cgroup control setting");
              exit(1);
          }

          setting->name  = STRDUP($1.value);
          setting->value = STRDUP($3.value);
          $$             = setting;
    }
    ;

control_value: TOKEN_IDENT { $$ = $1; }
    | TOKEN_STRING         { $$ = $1; }
    | TOKEN_SINT {
          $$.lineno = $1.lineno;
          $$.value  = (char *)$1.token;
    }
    | TOKEN_UINT {
          $$.lineno = $1.lineno;
          $$.value  = (char *)$1.token;
    }
    ;

priority_response_curve: KEYWORD_RESPONSE_CURVE KEYWORD_PRIORITY
                         double_range TOKEN_STRING integer_range
                         optional_integer_range integer_value {
        int prio_min, prio_max;
        
        prio_min = $6.min ? $6.min : -20;
        prio_max = $6.max ? $6.max :  19;

        if (prio_min < -20)
            prio_min = -20;
        if (prio_max > 19)
            prio_max = 19;

        ctx->prio_curve = curve_create($4.value,
                                       $3.min, $3.max,
                                       $5.min, $5.max,
                                       prio_min, prio_max);

        ctx->prio_default = $7.value;
    }
    ;

oom_response_curve: KEYWORD_RESPONSE_CURVE KEYWORD_OOM
                         double_range TOKEN_STRING integer_range
                         optional_integer_range integer_value {
        int oom_min, oom_max;
        
        oom_min = $6.set ? $6.min : -17;
        oom_max = $6.set ? $6.max :  15;

        if (oom_min < -17)
            oom_min = -17;
        if (oom_max > 15)
            oom_max = 15;

        ctx->oom_curve = curve_create($4.value,
                                      $3.min, $3.max,
                                      $5.min, $5.max,
                                      oom_min, oom_max);

        ctx->oom_default = $7.value;
    }
    ;

double_range: "[" double_value "," double_value "]" {
          $$.name  = NULL;
          $$.min = $2.value;
          $$.max = $4.value;
    }
    ;

integer_range: "[" integer_value "," integer_value "]" {
          $$.name  = NULL;
          $$.min = $2.value;
          $$.max = $4.value;
    }
    ;

optional_integer_range: /* empty */  {
          $$.name = NULL;
          $$.set  = FALSE;
          $$.min  = $$.max = 0;
      }
     | integer_range {
         $$     = $1;
         $$.set = TRUE;
       }
     ;


double_value: TOKEN_DOUBLE { $$ = $1; }
            | TOKEN_SINT {
                  $$.token  = $1.token;
                  $$.lineno = $1.lineno;
                  $$.value  = 1.0 * $1.value;
              }
            | TOKEN_UINT {
                  $$.token  = $1.token;
                  $$.lineno = $1.lineno;
                  $$.value  = 1.0 * $1.value;
              }
            ;

integer_value: TOKEN_SINT { $$ = $1; }
             | TOKEN_UINT {
                   $$.token  = $1.token;
                   $$.lineno = $1.lineno;
                   $$.value  = $1.value;
               }
             ;

/*****************************************************************************
 *                           *** partition section ***                       *
 *****************************************************************************/

partition_section: partition
    | partition_section partition
    ;

partition: "[" KEYWORD_PARTITION TOKEN_IDENT "]" "\n" partition_properties 
            optional_partition_controls {
      $6.name     = $3.value;
      $6.settings = $7;
      if (partition_lookup(ctx, $6.name) != NULL) {
          OHM_ERROR("cgrp: partition '%s' multiply defined", $6.name);
	  YYABORT;
      }

      if (partition_add(ctx, &$6) == NULL)
          YYABORT;
    }
    ;

partition_properties: partition_path "\n" {
          $$.path = $1.value;
    }
    | partition_export_fact "\n" {
          CGRP_SET_FLAG($$.flags, CGRP_PARTITION_FACT);
    }
    | partition_cpu_share "\n" {
          $$.limit.cpu = $1.value;
    }
    | partition_mem_limit "\n" {
          $$.limit.mem = $1.value;
    }
    | partition_properties partition_path "\n" {
          $$ = $1;
          $$.path = $2.value;
    }
    | partition_properties partition_export_fact "\n" {
          $$ = $1;
          CGRP_SET_FLAG($$.flags, CGRP_PARTITION_FACT);
    }
    | partition_properties partition_cpu_share "\n" {
          $$           = $1;
          $$.limit.cpu = $2.value;
    }
    | partition_properties partition_mem_limit "\n" {
          $$           = $1;
          $$.limit.mem = $2.value;
    }
    | partition_properties partition_rt_limit "\n" {
          $$                  = $1;
          $$.limit.rt_period  = $2.limit.rt_period;
          $$.limit.rt_runtime = $2.limit.rt_runtime;
    }
    | partition_properties error {
        OHM_ERROR("cgrp: failed to parse partition properties near token '%s'",
		  cgrpyylval.any.token);
        exit(1);
    }
    ;

partition_path: KEYWORD_PATH path { $$ = $2; }
    ;

partition_export_fact: KEYWORD_EXPORT_FACT
    ;

partition_cpu_share: KEYWORD_CPU_SHARES TOKEN_UINT { $$ = $2; }
    ;

partition_mem_limit: KEYWORD_MEM_LIMIT TOKEN_UINT optional_unit {
          $$        = $2;
	  $$.value *= $3.value;
    }
    ;

partition_rt_limit: KEYWORD_REALTIME_LIMIT 
                      TOKEN_IDENT time_usec TOKEN_IDENT time_usec {
          if (!strcmp($2.value, "period") &&
              !strcmp($4.value, "runtime")) {
	      $$.limit.rt_period  = $3;
	      $$.limit.rt_runtime = $5;
          }
	  else if (!strcmp($4.value, "period") &&
                   !strcmp($2.value, "runtime")) {
	      $$.limit.rt_period  = $5;
	      $$.limit.rt_runtime = $3;
          }
	  else {
              OHM_ERROR("cgrp: invalid realtime limits ('%s', '%s')",
	                $2.value, $4.value);
              exit(1);
          }
    }
    ;

optional_partition_controls: /* empty */ { $$ = NULL; }
    | partition_controls                 { $$ = $1;   }
    ;

partition_controls: partition_control {
          $$ = $1;
    }
    | partition_controls partition_control {
          cgrp_ctrl_setting_t *setting;

          for (setting = $1; setting->next != NULL; setting = setting->next)
              ;
          setting->next = $2;
          $$            = $1;
    }
    ;

partition_control: TOKEN_IDENT TOKEN_IDENT "\n" {
          cgrp_ctrl_setting_t *setting;

          if (ALLOC_OBJ(setting) == NULL) {
              OHM_ERROR("cgrp: failed to allocate partition control setting");
              exit(1);
          }

          setting->name  = STRDUP($1.value);
          setting->value = STRDUP($2.value);
          $$             = setting;
    }
    |  TOKEN_IDENT TOKEN_STRING "\n" {
          cgrp_ctrl_setting_t *setting;

          if (ALLOC_OBJ(setting) == NULL) {
              OHM_ERROR("cgrp: failed to allocate partition control setting");
              exit(1);
          }

          setting->name  = STRDUP($1.value);
          setting->value = STRDUP($2.value);
          $$             = setting;
    }
    |  TOKEN_IDENT TOKEN_UINT "\n" {
          cgrp_ctrl_setting_t *setting;

          if (ALLOC_OBJ(setting) == NULL) {
              OHM_ERROR("cgrp: failed to allocate partition control setting");
              exit(1);
          }

          setting->name  = STRDUP($1.value);
          setting->value = STRDUP($2.token);
          $$             = setting;
    }
    |  TOKEN_IDENT TOKEN_SINT "\n" {
          cgrp_ctrl_setting_t *setting;

          if (ALLOC_OBJ(setting) == NULL) {
              OHM_ERROR("cgrp: failed to allocate partition control setting");
              exit(1);
          }

          setting->name  = STRDUP($1.value);
          setting->value = STRDUP($2.token);
          $$             = setting;
    }
    ;

/*****************************************************************************
 *                             *** group section ***                         *
 *****************************************************************************/

group_section: group
    | group_section group
    ;

group: "[" KEYWORD_GROUP TOKEN_IDENT "]" "\n" group_properties {
      $6.name = $3.value;
      if (!group_add(ctx, &$6))
          YYABORT;
    }
    ;

group_properties:      group_description "\n" { $$ = $1; }
    |                  group_partition "\n" { $$ = $1; }
    |                  group_priority "\n" { $$ = $1; }
    |                  group_fact "\n" {
        CGRP_SET_FLAG($$.flags, CGRP_GROUPFLAG_FACT);
    }
    | group_properties group_description "\n" {
        $$             = $1;
        $$.description = $2.description;
    }
    | group_properties group_partition "\n" {
        $$            = $1;
        $$.partition  = $2.partition;
	CGRP_SET_FLAG($$.flags, CGRP_GROUPFLAG_STATIC);
    }
    | group_properties group_priority "\n" {
        $$            = $1;
        $$.priority   = $2.priority;
        CGRP_SET_FLAG($$.flags, CGRP_GROUPFLAG_PRIORITY);
    }
    | group_properties group_fact "\n" {
        $$        = $1;
        CGRP_SET_FLAG($$.flags, CGRP_GROUPFLAG_FACT);
    }
    | group_properties error {
        OHM_ERROR("cgrp: failed to parse group properties near token '%s'",
		  cgrpyylval.any.token);
        exit(1);
    }
    ;

group_description: KEYWORD_DESCRIPTION TOKEN_STRING {
        memset(&$$, 0, sizeof($$));
        $$.description = $2.value;
    }
    ;

group_partition: KEYWORD_PARTITION TOKEN_IDENT {
        memset(&$$, 0, sizeof($$));
	if (($$.partition = partition_lookup(ctx, $2.value)) == NULL) {
	    OHM_ERROR("cgrp: nonexisting partition '%s' in a group", $2.value);
	    exit(1);
	}
    }
    ;

group_priority: KEYWORD_PRIORITY TOKEN_SINT {
        int priority = $2.value;
        memset(&$$, 0, sizeof($$));
        $$.priority = priority < -20 ? -20 : (priority > 19 ? 19: priority);
    }
    ;

group_fact: KEYWORD_EXPORT_FACT
    ;


/*****************************************************************************
 *                             *** rule section ***                          *
 *****************************************************************************/

procdef_section: procdef
    | procdef_section procdef
    | procdef_section error {
          OHM_ERROR("cgrp: failed to parse rule section near token '%s'",
	            cgrpyylval.any.token);
          exit(1);
    }
    ;

procdef: "[" KEYWORD_RULE procdef_path "]" "\n" 
         optional_renice rules optional_newline {
        cgrp_procdef_t procdef;

        procdef.binary = $3.value;
        procdef.rules  = $7;

        if (CGRP_TST_FLAG(ctx->options.flags, CGRP_FLAG_ADDON_RULES))
            addon_add(ctx, &procdef);
        else {
            if (!procdef_add(ctx, &procdef))
                YYABORT;
        }
    }
    ;

optional_renice: /* empty */
    | KEYWORD_RENICE TOKEN_SINT {
          OHM_WARNING("cgrp: static renice not supported any more.");
      }
    ;

procdef_path: TOKEN_PATH       { $$ = $1; }
    |      TOKEN_STRING        { $$ = $1; }
    |      TOKEN_ASTERISK      { $$.value = "*"; }
    ;


rules: rule {
          $$ = $1;
      }
    |  rules optional_newline rule optional_newline {
          cgrp_rule_t *r;

          for (r = $1; r->next != NULL; r = r->next)
              ;
          r->next = $3;
          $$      = $1;
       }
    ;


optional_newline: /* empty */
    | newline
    ;

newline: "\n"
    ;

rule: "<" rule_events ">" optional_newline 
      "{" optional_newline rule_statements optional_newline "}" {
          $2->statements = $7;
          $$ = $2;
      }
    | rule_statements {
          cgrp_rule_t *rule;

          if (ALLOC_OBJ(rule) == NULL) {
              OHM_ERROR("cgrp: failed to allocate new rule");
              exit(1);
          }

          rule->event_mask = (1 << CGRP_EVENT_EXEC);
          rule->statements = $1;

          $$ = rule;
      }
    ;

rule_events: rule_event {
       cgrp_rule_t *rule;

       if (ALLOC_OBJ(rule) == NULL) {
           OHM_ERROR("cgrp: failed to allocate new rule");
           exit(1);
       }

       if ($1.event_mask & (1 << CGRP_EVENT_GID)) {
           if ($1.ngid == 0)                       /* wildcard group */
               add_grp(rule, "*");
           else
               add_gid(rule, (gid_t)$1.gids);
       }
       else if ($1.event_mask & (1 << CGRP_EVENT_UID)) {
           if ($1.nuid == 0)                       /* wildcard user */
               add_usr(rule, "*");
           else
               add_uid(rule, (uid_t)$1.uids);
       }
       else
           rule->event_mask = $1.event_mask;
       
       $$ = rule;
    }
    | rule_events "," rule_event {
       if (!($3.event_mask & ((1 << CGRP_EVENT_GID) | (1 << CGRP_EVENT_UID)))) {
           $1->event_mask |= $3.event_mask;
           $$ = $1;
       }
       else {
           if ($3.event_mask & (1 << CGRP_EVENT_GID)) {
               if ($3.ngid == 0)                      /* wildcard group */
                   add_grp($1, "*");
               else
                   add_gid($1, (gid_t)$3.gids);
           }
           else { /* $3->event_mask & (1 << CGRP_EVENT_UID) */
               if ($3.nuid == 0)                      /* wildcard user */
                   add_usr($1, "*");
               else
                   add_uid($1, (uid_t)$3.uids);
           }
       }

       $$ = $1;
    }
    ;

rule_event: TOKEN_IDENT {
          memset(&$$, 0, sizeof($$));

          if (!strcmp($1.value, "execed"))
              $$.event_mask = (1 << CGRP_EVENT_EXEC);
          else if (!strcmp($1.value, "new-thread"))
              $$.event_mask = (1 << CGRP_EVENT_THREAD);
          else if (!strcmp($1.value, "session-change"))
              $$.event_mask = (1 << CGRP_EVENT_SID);
          else if (!strcmp($1.value, "comm-change"))
              $$.event_mask = (1 << CGRP_EVENT_COMM);
          else {
              OHM_ERROR("cgrp: invalid rule event '%s'", $1.value);
              YYABORT;
          }
      }
    | TOKEN_IDENT TOKEN_UINT {
          memset(&$$, 0, sizeof($$));

          if (!strcmp($1.value, "group-change")) {
              $$.event_mask = (1 << CGRP_EVENT_GID);
              $$.gids       = (gid_t *)$2.value;
              $$.ngid       = 1;
          }
          else if (!strcmp($1.value, "user-change")) {
              $$.event_mask = (1 << CGRP_EVENT_UID);
              $$.uids       = (uid_t *)$2.value;
              $$.nuid       = 1;
          }
          else {
              OHM_ERROR("cgrp: invalid rule event '%s'", $1.value);
              YYABORT;
          }
      }
    | TOKEN_IDENT string_or_wildcard {
          const char *name = $2.value;

          memset(&$$, 0, sizeof($$));

          if (!strcmp($1.value, "group-change")) {
              $$.event_mask = (1 << CGRP_EVENT_GID);

              if (!strcmp(name, "*")) {
                  $$.gids = NULL;
                  $$.ngid = 0;                     /* wildcard */
              }
              else {
                  if (lookup_gid(name, (gid_t *)&$$.gids))
                      $$.ngid = 1;
                  else {
                      OHM_ERROR("cgrp: ignoring unknown group '%s'", name);
                      $$.event_mask = 0;
                  }
              }
          }
          else if (!strcmp($1.value, "user-change")) {
              $$.event_mask = (1 << CGRP_EVENT_UID);

              if (!strcmp(name, "*")) {
                  $$.uids = NULL;
                  $$.nuid = 0;                     /* wildcard */
              }
              else {
                  if (lookup_uid(name, (uid_t *)&$$.uids))
                      $$.nuid = 1;
                  else {
                      OHM_ERROR("cgrp: ignoring unknown user '%s'", name);
                      $$.event_mask = 0;
                  }
              }
          }
          else {
              OHM_ERROR("cgrp: invalid rule event '%s'", $1.value);
              YYABORT;
          }
      }
    ;


string_or_wildcard: string { $$       = $1;  }
   |    TOKEN_ASTERISK     { $$.value = "*"; }
   ;

rule_statements: rule_statement "\n" {
        $$ = $1;
    }
    | rule_statements rule_statement "\n" {
        cgrp_stmt_t *stmt;

        for (stmt = $1; stmt->next != NULL; stmt = stmt->next)
	    ;
	stmt->next = $2;
        $$         = $1;
    }
    | rule_statements error {
          OHM_ERROR("cgrp: failed to parse rule statements real '%s'",
                    cgrpyylval.any.token);
          exit(1);
    }
    ;

rule_statement: expr "=>" actions {
        cgrp_stmt_t *stmt;

        if (ALLOC_OBJ(stmt) == NULL) {
            OHM_ERROR("cgrp: failed to allocate statement");
            exit(1);
        }
        stmt->expr    = $1;
        stmt->actions = $3;

        $$ = stmt;
    }
    | actions {
        cgrp_stmt_t *stmt;

        if (ALLOC_OBJ(stmt) == NULL) {
            OHM_ERROR("cgrp: failed to allocate statement");
            exit(1);
        }
        stmt->expr    = NULL;
        stmt->actions = $1;

        $$ = stmt;
    }
    ;

expr: bool_expr    { $$ = $1; }
    | prop_expr    { $$ = $1; }
    | "(" expr ")" { $$ = $2; }
    ;

bool_expr: expr "||" expr { $$ = bool_expr(CGRP_BOOL_OR , $1, $3);   }
    |      expr "&&" expr { $$ = bool_expr(CGRP_BOOL_AND, $1, $3);   }
    |      "!" expr       { $$ = bool_expr(CGRP_BOOL_NOT, $2, NULL); }
    ;

prop_expr: prop "==" value { $$ = prop_expr($1, CGRP_OP_EQUAL, &$3); }
    |      prop "!=" value { $$ = prop_expr($1, CGRP_OP_NOTEQ, &$3); }
    |      prop "<"  value { $$ = prop_expr($1, CGRP_OP_LESS, &$3); }
    ;

prop: TOKEN_ARG          { $$ = CGRP_PROP_ARG($1.value); }
    | KEYWORD_BINARY     { $$ = CGRP_PROP_BINARY;        }
    | KEYWORD_CMDLINE    { $$ = CGRP_PROP_CMDLINE;       }
    | KEYWORD_NAME       { $$ = CGRP_PROP_NAME;          }
    | KEYWORD_TYPE       { $$ = CGRP_PROP_TYPE;          }
    | KEYWORD_USER       { $$ = CGRP_PROP_EUID;          }
    | KEYWORD_GROUP      { $$ = CGRP_PROP_EGID;          }
    | KEYWORD_PARENT     { $$ = CGRP_PROP_PARENT;        }
    | KEYWORD_RECLASSIFY { $$ = CGRP_PROP_RECLASSIFY;    }
    ;

value: TOKEN_STRING {
        $$.type = CGRP_VALUE_TYPE_STRING;
	$$.str  = STRDUP($1.value);
    }
    | TOKEN_IDENT {
        $$.type = CGRP_VALUE_TYPE_STRING;
	$$.str  = STRDUP($1.value);
    }
    | TOKEN_PATH {
        $$.type = CGRP_VALUE_TYPE_STRING;
	$$.str  = STRDUP($1.value);
    }
    | TOKEN_UINT {
        $$.type = CGRP_VALUE_TYPE_UINT32;
        $$.u32  = $1.value;
    }
    | TOKEN_SINT {
        $$.type = CGRP_VALUE_TYPE_SINT32;
        $$.s32  = $1.value;
    }
    ;


procdef: "[" KEYWORD_CLASSIFY string "]"
         { strcpy(rule_group, $3.value); } "\n"
           simple_rule_list               { rule_group[0] = '\0'; }
    ;

simple_rule_list: simple_rule "\n"
    | simple_rule_list simple_rule "\n"
    | simple_rule_list error {
          OHM_ERROR("cgrp: failed to parse simple rule section near token '%s'",
	            cgrpyylval.any.token);
          YYABORT;
    }
    ;

simple_rule: path {
        cgrp_procdef_t  procdef;
	cgrp_rule_t    *rule;
        cgrp_stmt_t    *stmt;
	cgrp_action_t  *action;
	cgrp_group_t   *group = group_find(ctx, rule_group);

        if (group == NULL) {
            OHM_ERROR("cgrp: reference to unknown group '%s'", rule_group);
	    YYABORT;
        }

        action = action_group_new(group);
	if (action == NULL) {
	    OHM_ERROR("cgrp: failed to allocate new group action");
	    YYABORT;
        }

        if (ALLOC_OBJ(stmt) == NULL) {
            OHM_ERROR("cgrp: failed to allocate statement");
	    action_del(action);
            YYABORT;
        }
        stmt->expr    = NULL;
        stmt->actions = action;

        if (ALLOC_OBJ(rule) == NULL) {
            OHM_ERROR("cgrp: failed to allocate rule");
            statement_free_all(stmt);
	    YYABORT;
        }
        rule->event_mask = (1 << CGRP_EVENT_EXEC) | (1 << CGRP_EVENT_THREAD);
        rule->statements = stmt;

        procdef.binary = $1.value;
        procdef.rules  = rule;

        if (CGRP_TST_FLAG(ctx->options.flags, CGRP_FLAG_ADDON_RULES))
            addon_add(ctx, &procdef);
        else {
            if (!procdef_add(ctx, &procdef))
                YYABORT;
        }
    }
    ;


/*****************************************************************************
 *                      *** classifiaction actions ***                       *
 *****************************************************************************/

actions: action          { $$ = $1; }
    | actions ";" action { $$ = $1; action_add($$, $3); }
    ;

action: action_group     { $$ = $1; }
    |   action_classify  { $$ = $1; }
    |   action_schedule  { $$ = $1; }
    |   action_renice    { $$ = $1; }
    |   action_priority  { $$ = $1; }
    |   action_oom       { $$ = $1; }
    |   action_ignore    { $$ = $1; }
    |   action_leads     { $$ = $1; }
    |   action_no_op     { $$ = $1; }
    ;

action_group: KEYWORD_GROUP string {
	cgrp_action_t *action;
	cgrp_group_t  *group = group_find(ctx, $2.value);

        if (group == NULL) {
            OHM_ERROR("cgrp: reference to unknown group '%s'", $2.value);
	    YYABORT;
        }

        action = action_group_new(group);
	if (action == NULL) {
	    OHM_ERROR("cgrp: failed to allocate new group action");
	    YYABORT;
        }

        $$ = action;
    }
    ;

action_classify: KEYWORD_RECLASS_AFTER TOKEN_UINT {
        cgrp_action_t *action;

        action = action_classify_new($2.value);
        if (action == NULL) {
            OHM_ERROR("cgrp: failed to allocate new classify action");
            YYABORT;
        }

        $$ = action;
    }
    ;


action_classify: KEYWORD_CLASSIFY_ARGVX {
        cgrp_action_t *action;

        action = action_classify_new(-1 - $1.value);
        if (action == NULL) {
            OHM_ERROR("cgrp: failed to allocate new classify-by-argv0 action");
            YYABORT;
        }

        $$ = action;
    }
    ;


action_schedule: KEYWORD_SCHEDULE TOKEN_IDENT schedule_priority {
        cgrp_action_t *action;

        action = action_schedule_new($2.value, $3.value);
        if (action == NULL) {
            OHM_ERROR("cgrp: failed to allocate new schedule action");
            YYABORT;
        }

        $$ = action;
    }
    ;

action_renice: KEYWORD_RENICE renice_priority {
        cgrp_action_t *action;

        action = action_renice_new($2.value);
        if (action == NULL) {
            OHM_ERROR("cgrp: failed to allocate new renice action");
            YYABORT;
        }

        $$ = action;
    }
    ;

action_priority: KEYWORD_PRIORITY adjust_action adjust_value {
        cgrp_action_t *action;

        action = action_priority_new($2, $3.value);
        if (action == NULL) {
            OHM_ERROR("cgrp: failed to allocate new priority action");
            YYABORT;
        }

        $$ = action;
    }
    ;

action_oom: KEYWORD_OOM adjust_action adjust_value {
        cgrp_action_t *action;

        action = action_oom_new($2, $3.value);
        if (action == NULL) {
            OHM_ERROR("cgrp: failed to allocate new OOM action");
            YYABORT;
        }

        $$ = action;
    }
    ;

action_ignore: KEYWORD_IGNORE {
        cgrp_action_t *action;

        action = action_ignore_new();
        if (action == NULL) {
            OHM_ERROR("cgrp: failed to allocate new ignore action");
            YYABORT;
        }

        $$ = action;
    }
    ;

action_leads: KEYWORD_LEADS followers {
        cgrp_action_t *action;

        action = action_leads_new($2);
        if (action == NULL) {
            OHM_ERROR("cgrp: failed to allocate new leads action");
            YYABORT;
        }

        $$ = action;
    }
    ;

action_no_op: KEYWORD_NO_OP {
        cgrp_action_t *action;

        action = action_noop_new();
        if (action == NULL) {
            OHM_ERROR("cgrp: failed to allocate new no-op action");
            YYABORT;
        }

        $$ = action;
    }
    ;

schedule_priority: /* empty */ { $$.value = 0; }
    | TOKEN_UINT               { $$ = $1;      }
    ;

renice_priority: TOKEN_UINT    { $$.value = $1.value; }
    |            TOKEN_SINT    { $$.value = $1.value; }
    ;

adjust_action: TOKEN_IDENT  { $$ = parse_adjust($1.value); }
    |          TOKEN_STRING { $$ = parse_adjust($1.value); }
    ;

adjust_value: TOKEN_SINT { $$.value = $1.value;      }
    |         TOKEN_UINT { $$.value = (int)$1.value; }
    ;

followers: follower {
        $$ = $1;
    }
    | followers "," follower {
        cgrp_follower_t *follower;

        for (follower = $1; follower->next != NULL; follower = follower->next)
            ;
        follower->next = $3;
        $$             = $1;
    }
    ;

follower: path {
        cgrp_follower_t *follower;

        if (ALLOC_OBJ(follower) == NULL) {
            OHM_ERROR("cgrp: failed to allocate a follower object");
            exit(1);
        }

        follower->name = STRDUP($1.value);
        $$ = follower;
    }
    ;

/*****************************************************************************
 *                        *** miscallaneous rules ***                        *
 *****************************************************************************/

string: TOKEN_IDENT  { $$ = $1; }
    |   TOKEN_STRING { $$ = $1; }
    ;


path: TOKEN_PATH   { $$ = $1; }
    | TOKEN_STRING { $$ = $1; }
    ;


optional_unit: /* empty */ { $$.value = 1; }
    | TOKEN_IDENT         {
          if ($1.value[1] != '\0')
              goto invalid;

          switch ($1.value[0]) {
              case 'k': case 'K': $$.value = 1024;        break;
              case 'm': case 'M': $$.value = 1024 * 1024; break;
              default:
              invalid:
	          OHM_ERROR("cgrp: invalid memory limit unit '%s'", $1.value);
	          exit(1);
          }
    }
    ;


time_usec: TOKEN_UINT time_unit {
          if ($2 == 0)
	      $2 = 1000000;
          $$ = (s64_t)$1.value * $2;
    }
    | TOKEN_SINT time_unit {
          if ($2 == 0)
	      $2 = 1000000;
          $$ = $1.value * $2;
    }
    ;

time_unit: /* use default units */ {
          $$ = 0;
    }
    | TOKEN_IDENT {
          if      (!strcmp($1.value,  "sec")) $$ = 1000 * 1000;
	  else if (!strcmp($1.value, "msec")) $$ = 1000;
          else if (!strcmp($1.value, "usec")) $$ = 1;
          else {
              OHM_ERROR("cgrp: invalid time unit '%s'", $1.value);
	      exit(1);
          }
    }
    ;

%%

/*****************************************************************************
 *                        *** parser public interface ***                    *
 *****************************************************************************/

/********************
 * config_parse_config
 ********************/
int
config_parse_config(cgrp_context_t *ctx, char *path)
{
    if (access(path, F_OK) != 0 && errno == ENOENT) {
        OHM_WARNING("cgrp: no configuration file found");
        return TRUE;
    }

    lexer_reset(START_FULL_PARSER);

    if (!lexer_push_input(path))
	return FALSE;

    return cgrpyyparse(ctx) == 0;
}


/********************
 * config_parse_addon
 ********************/
int
config_parse_addon(cgrp_context_t *ctx, char *path)
{
    int success;

    lexer_reset(START_ADDON_PARSER);
    if (!lexer_push_input(path))
        return FALSE;

    CGRP_SET_FLAG(ctx->options.flags, CGRP_FLAG_ADDON_RULES);
    lexer_disable_include();
    success = cgrpyyparse(ctx) == 0;
    lexer_enable_include();
    CGRP_CLR_FLAG(ctx->options.flags, CGRP_FLAG_ADDON_RULES);

    return success;
}


/********************
 * config_parse_addons
 ********************/
int
config_parse_addons(cgrp_context_t *ctx)
{
    char           glob[PATH_MAX], pattern[PATH_MAX];
    char           dir[PATH_MAX], file[PATH_MAX];
    char          *path, *base, *p, *q;
    int            len;
    DIR           *dp;
    struct dirent *de;
    struct stat    st;
    regex_t        regex;
    regmatch_t     m;
    
    path = ctx->options.addon_rules;
    if (path == NULL)
        return TRUE;
    
    if ((base = strrchr(path, '/')) != NULL) {
        if (((p = strchr(path, '*')) != NULL && p < base) ||
            ((p = strchr(path, '?')) != NULL && p < base)) {
            OHM_ERROR("cgrp: invalid addon rule pattern '%s'", path);
            return FALSE;
        }
        
        for (p = base; *p == '/'; p++)
            ;
        strncpy(glob, p, sizeof(glob) - 1);
        glob[sizeof(glob) - 1] = '\0';

        while (*base == '/' && base > path)
            base--;

        len = base - path + 1;
        strncpy(dir, path, len);
        dir[len] = '\0';
    }
    else {
        OHM_ERROR("cgrp: invalid addon rule pattern '%s'", path);
        return FALSE;
    }
    
    for (p = glob, q = pattern; *p; p++) {
        switch (*p) {
        case '*': *q++ = '.';  *q++ = '*'; break;
        case '?': *q++ = '.';              break;
        case '.': *q++ = '\\'; *q++ = '.'; break;
        default:  *q++ = *p;               break;
        }
    }
    *q = '\0';

    if (regcomp(&regex, pattern, /*REG_NOSUB |*/ REG_NEWLINE) != 0) {
        OHM_ERROR("cgrp: failed to compile regexp '%s' for '%s'",
                  pattern, glob);
        return FALSE;
    }
    
    if ((dp = opendir(dir)) == NULL) {
        regfree(&regex);
        return TRUE;
    }

    while ((de = readdir(dp)) != NULL) {
        snprintf(file, sizeof(file), "%s/%s", dir, de->d_name);
        
        if (stat(file, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        
        if (!regexec(&regex, de->d_name, 1, &m, REG_NOTBOL|REG_NOTEOL) &&
            m.rm_so == 0 && m.rm_eo == (regoff_t)strlen(de->d_name)) {
            config_parse_addon(ctx, file);
        }
    }
    
    closedir(dp);
    regfree(&regex);
    
    return TRUE;
}


/********************
 * config_change_cb
 ********************/
static gboolean
config_change_cb(GIOChannel *chnl, GIOCondition mask, gpointer data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;
    struct { 
        struct inotify_event event;
        char                 path[PATH_MAX];
    } event;

    (void)chnl;

    
    if (mask & (G_IO_IN | G_IO_PRI)) {
        read(ctx->addonwd, &event, sizeof(event));
        
        OHM_DEBUG(DBG_CONFIG,
                  "configuration updated (event 0x%x), scheduling reload",
                  event.event.mask);
        
        config_schedule_reload(ctx);
    }

    return TRUE;
}


/********************
 * config_schedule_reload
 ********************/
gboolean
reload_config(gpointer data)
{
    cgrp_context_t *ctx = (cgrp_context_t *)data;
    
    OHM_INFO("cgrp: reloading addon classification rules");

    addon_reload(ctx);
    ctx->addontmr = 0;
    
    OHM_INFO("cgrp: reclassifying existing processes");
    process_scan_proc(ctx);
    
    return FALSE;
}

void
config_schedule_reload(cgrp_context_t *ctx)
{
    if (ctx->addontmr != 0)
        g_source_remove(ctx->addontmr);
    
    ctx->addontmr = g_timeout_add(15 * 1000, reload_config, ctx);
}


/********************
 * config_monitor_init
 ********************/
int
config_monitor_init(cgrp_context_t *ctx)
{
    char         dir[PATH_MAX], *end;
    uint32_t     eventmask;
    GIOCondition condmask;
    
    if (ctx->options.addon_rules == NULL)
        return TRUE;

    if (!CGRP_TST_FLAG(ctx->options.flags, CGRP_FLAG_ADDON_MONITOR))
        return TRUE;

    strncpy(dir, ctx->options.addon_rules, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    
    if ((end = strrchr(dir, '/')) == NULL)
        return FALSE;
    while (*end == '/' && end > dir)
        *end-- = '\0';

    if ((access(dir, F_OK) != 0 && errno == ENOENT)) {
        OHM_WARNING("cgrp: non-existing add-on rule directory '%s'", dir);
        return FALSE;
    }

    if ((ctx->addonwd = inotify_init()) < 0) {
        OHM_ERROR("cgrp: failed to create inotify watch for addon rules");
        return FALSE;
    }

    eventmask = IN_CLOSE_WRITE | IN_DELETE | IN_MOVE;
    if (inotify_add_watch(ctx->addonwd, dir, eventmask) < 0) {
        OHM_ERROR("cgrp: failed to set up inotify addon rules monitoring");
        return FALSE;
    }

    if ((ctx->addonchnl = g_io_channel_unix_new(ctx->addonwd)) == NULL) {
        OHM_ERROR("cgrp: failed to allocate watch for addon rules");
        return FALSE;
    }
    
    condmask = G_IO_IN | G_IO_HUP | G_IO_PRI | G_IO_ERR;
    ctx->addonsrc = g_io_add_watch(ctx->addonchnl, condmask,
                                   config_change_cb, ctx);
    
    return ctx->addonsrc != 0;
}


/********************
 * config_monitor_exit
 ********************/
void
config_monitor_exit(cgrp_context_t *ctx)
{
    if (ctx->addonsrc != 0) {
        g_source_remove(ctx->addonsrc);
        ctx->addonsrc = 0;
    }
    
    if (ctx->addonchnl != NULL) {
        g_io_channel_unref(ctx->addonchnl);
        ctx->addonchnl = NULL;
    }

    if (ctx->addonwd > 0) {
        close(ctx->addonwd);
        ctx->addonwd = -1;
    }
}


/********************
 * config_dump
 ********************/
void
config_print(cgrp_context_t *ctx, FILE *fp)
{
    const char *prio;
    int         flags;

    flags = ctx->options.flags;

    if (flags) {
        fprintf(fp, "# global configuration flags:\n");
        
        if (CGRP_TST_FLAG(flags, CGRP_FLAG_GROUP_FACTS))
            fprintf(fp, "export-group-facts\n");

        if (CGRP_TST_FLAG(flags, CGRP_FLAG_PART_FACTS))
            fprintf(fp, "export-partition-facts\n");

        if (CGRP_TST_FLAG(flags, CGRP_FLAG_ALWAYS_FALLBACK))
            fprintf(fp, "always-fallback\n");

        switch (ctx->options.prio_preserve) {
        case CGRP_PRIO_ALL:  prio = ALL_PRIO; break;
        case CGRP_PRIO_LOW:  prio = LOW_PRIO; break;
        case CGRP_PRIO_NONE: prio = NO_PRIO;  break;
        default:             prio = "<?>";  break;
        }            

        fprintf(fp, "preserve-priority %s\n", prio);
    }
    
    /* XXX TODO: add dumping all other options, too... */

    ctrl_dump(ctx, fp);
    partition_dump(ctx, fp);
    group_dump(ctx, fp);
    procdef_dump(ctx, fp);
}


void
cgrpyyerror(cgrp_context_t *ctx, const char *msg)
{
    (void)ctx;

    OHM_ERROR("parse error: %s near line %d in file %s", msg,
              lexer_line(), lexer_file());
}


/********************
 * gid/uid routines
 ********************/

static int
add_gid(cgrp_rule_t *rule, gid_t gid)
{
    if ((rule->event_mask & (1 << CGRP_EVENT_GID)) && rule->ngid == 0)
        return TRUE;

    if (REALLOC_ARR(rule->gids, rule->ngid, rule->ngid + 1) != NULL) {
        rule->event_mask |= (1 << CGRP_EVENT_GID);
        rule->gids[rule->ngid++] = gid;
        
        return TRUE;
    }
    else {
        OHM_ERROR("cgrp: failed to allocate new group id for rule events");
        
        return FALSE;
    }
}


static int
add_grp(cgrp_rule_t *rule, const char *name)
{
    gid_t gid;
    
    if ((rule->event_mask & (1 << CGRP_EVENT_GID)) && rule->ngid == 0)
        return TRUE;

    if (!strcmp(name, "*")) {
        if (rule->gids != NULL) {
            FREE(rule->gids);
            rule->gids = NULL;
        }

        rule->ngid        = 0;                 /* mark as wildcard group */
        rule->event_mask |= (1 << CGRP_EVENT_GID);
        
        return TRUE;
    }
    else {
        if (!lookup_gid(name, &gid)) {
            OHM_ERROR("cgrp: failed to find group ID for '%s'", name);

            return FALSE;
        }
 
        return add_gid(rule, gid);
    }
}


static int
lookup_gid(const char *name, gid_t *gid)
{
    long         scnf = sysconf(_SC_GETGR_R_SIZE_MAX);
    size_t       size = (scnf < 0 ? 1024 : (size_t)scnf);
    struct group grp, *found;
    char         buf[size];
    
    if (getgrnam_r(name, &grp, buf, size, &found) != 0 || !found)
        return FALSE;
    else {
        *gid = grp.gr_gid;
        return TRUE;
    }
}


static int
add_uid(cgrp_rule_t *rule, uid_t uid)
{
    if ((rule->event_mask & (1 << CGRP_EVENT_UID)) && rule->nuid == 0)
        return TRUE;

    if (REALLOC_ARR(rule->uids, rule->nuid, rule->nuid + 1) != NULL) {
        rule->event_mask |= (1 << CGRP_EVENT_UID);
        rule->gids[rule->nuid++] = uid;
        
        return TRUE;
    }
    else {
        OHM_ERROR("cgrp: failed to allocate new user id for rule events");
        
        return FALSE;
    }
}


static int
add_usr(cgrp_rule_t *rule, const char *name)
{
    uid_t uid;
    
    if ((rule->event_mask & (1 << CGRP_EVENT_UID)) && rule->nuid == 0)
        return TRUE;

    if (!strcmp(name, "*")) {
        if (rule->uids != NULL) {
            FREE(rule->uids);
            rule->uids = NULL;
        }

        rule->nuid        = 0;                  /* mark as wildcard user */
        rule->event_mask |= (1 << CGRP_EVENT_UID);
        
        return TRUE;
    }
    else {
        if (!lookup_uid(name, &uid)) {
            OHM_ERROR("cgrp: failed to find user ID for '%s'", name);

            return FALSE;
        }
    
        return add_uid(rule, uid);
    }
}


static int
lookup_uid(const char *name, uid_t *uid)
{
    long          scnf = sysconf(_SC_GETPW_R_SIZE_MAX);
    size_t        size = (scnf < 0 ? 1024 : (size_t)scnf);
    struct passwd usr, *found;
    char          buf[size];
    
    if (getpwnam_r(name, &usr, buf, size, &found) != 0 || !found)
        return FALSE;
    else {
        *uid = usr.pw_uid;
        return TRUE;
    }
}


/********************
 * misc. parsing
 ********************/
static cgrp_adjust_t
parse_adjust(const char *action)
{
    if      (!strcmp(action, CGRP_ADJUST_ABSOLUTE)) return CGRP_ADJ_ABSOLUTE;
    else if (!strcmp(action, CGRP_ADJUST_RELATIVE)) return CGRP_ADJ_RELATIVE;
    else if (!strcmp(action, CGRP_ADJUST_LOCK    )) return CGRP_ADJ_LOCK;
    else if (!strcmp(action, CGRP_ADJUST_UNLOCK  )) return CGRP_ADJ_UNLOCK;
    else if (!strcmp(action, CGRP_ADJUST_EXTERN  )) return CGRP_ADJ_EXTERN;
    else if (!strcmp(action, CGRP_ADJUST_INTERN  )) return CGRP_ADJ_INTERN;
    
    return CGRP_ADJ_UNKNOWN;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
