%{

#include <stdio.h>

#include "cgrp-plugin.h"
#include "cgrp-parser-types.h"
#include "mm.h"  

int  yylex  (void);
void yyerror(cgrp_context_t *, const char *);

%}

%union {
    token_string_t    any;
    token_string_t    string;
    token_uint32_t    uint32;
    cgrp_partition_t  part;
    cgrp_partition_t *parts;
    cgrp_group_t      group;
    cgrp_group_t     *groups;
    cgrp_procdef_t    procdef;
    cgrp_procdef_t   *procdefs;
    cgrp_stmt_t      *stmt;
    cgrp_expr_t      *expr;
    cgrp_cmd_t        cmd;
    cgrp_prop_type_t  prop;
    cgrp_value_t      value;
    cgrp_context_t    ctx;
}

%defines
%parse-param {cgrp_context_t *ctx}

%type <part>     partition
%type <part>     partition_properties
%type <string>   partition_path
%type <string>   path
%type <group>    group
%type <group>    group_properties
%type <group>    group_description
%type <group>    group_partition
%type <procdef>  procdef
%type <string>   procdef_name
%type <stmt>     procdef_statements
%type <stmt>     procdef_statement
%type <expr>     expr
%type <expr>     bool_expr
%type <expr>     prop_expr
%type <prop>     prop
%type <value>    value
%type <cmd>      command
%type <cmd>      command_group
%type <cmd>      command_ignore
%type <string>   group_name

%token TOKEN_KW_GLOBAL
%token TOKEN_KW_PARTITION
%token TOKEN_KW_PATH
%token TOKEN_KW_GROUP
%token TOKEN_KW_DESCRIPTION
%token TOKEN_KW_PROCESS
%token TOKEN_KW_IGNORE

%token TOKEN_ASTERISK "*"
%token TOKEN_HEADER_OPEN "["
%token TOKEN_HEADER_CLOSE "]"

%token TOKEN_AND   "&&"
%token TOKEN_OR    "||"
%token TOKEN_NOT   "!"
%token TOKEN_EQUAL "=="
%token TOKEN_NOTEQ "!="
%token TOKEN_IMPLIES "=>"

%token TOKEN_EOL


%token <uint32> TOKEN_ARG
%token <string> TOKEN_IDENT
%token <string> TOKEN_PATH
%token <string> TOKEN_STRING


%%

configuration: partitions groups procdefs

partitions: partition
    | partitions partition
    ;

partition: "[" TOKEN_KW_PARTITION TOKEN_IDENT "]"
           partition_properties {
	$5.name = $3.value;
        if (!partition_add(ctx, &$5))
	    YYABORT;
    }
    ;

partition_properties: partition_path { $$.path = $1.value; }
    ;

partition_path: TOKEN_KW_PATH path { $$ = $2; }
    ;

path: TOKEN_PATH   { $$ = $1; }
    | TOKEN_STRING { $$ = $1; }
    ;

groups: group
    | groups group
    ;

group: "[" TOKEN_KW_GROUP TOKEN_IDENT "]"
       group_properties {
        $5.name = $3.value;
        if (!group_add(ctx, &$5))
	    YYABORT;
    }
    ;

group_properties: group_description { $$ = $1; }
    |             group_partition   { $$ = $1; }
    | group_properties group_description {
        $$             = $1;
        $$.description = $2.description;
    }
    | group_properties group_partition {
        $$            = $1;
        $$.partition  = $2.partition;
	$$.flags     |= CGRP_GROUPFLAG_STATIC;
    }
    ;

group_description: TOKEN_KW_DESCRIPTION TOKEN_STRING {
        memset(&$$, 0, sizeof($$));
        $$.description = $2.value;
    }
    ;

group_partition: TOKEN_KW_PARTITION TOKEN_IDENT {
        memset(&$$, 0, sizeof($$));
	if (($$.partition = partition_find(ctx, $2.value)) == NULL) {
	    OHM_ERROR("cgrp: nonexisting partition '%s' in a group", $2.value);
	    exit(1);
	}
    }
    ;

procdefs: procdef
    | procdefs procdef
    ;

procdef: "[" TOKEN_KW_PROCESS procdef_name "]"
         procdef_statements {
         cgrp_procdef_t procdef;

         procdef.binary     = $3.value;
         procdef.statements = $5;
         if (!procdef_add(ctx, &procdef))
	     YYABORT;
    }
    ;

procdef_name: TOKEN_IDENT { $$ = $1; }
    | TOKEN_PATH          { $$ = $1; }
    | TOKEN_ASTERISK      { $$.value = "*"; }
    ;

procdef_statements: procdef_statement {
        $$ = $1;
    }
    | procdef_statements procdef_statement {
        cgrp_stmt_t *stmt;

        for (stmt = $1; stmt->next != NULL; stmt = stmt->next)
	    ;
	stmt->next = $2;
        $$         = $1;
    }
    ;

procdef_statement: expr "=>" command {
        cgrp_stmt_t *stmt;
        if (ALLOC_OBJ(stmt) == NULL) {
            OHM_ERROR("cgrp: failed to allocate statement");
            exit(1);
        }
        stmt->expr    = $1;
        stmt->command = $3;

        $$ = stmt;
    }
    | command {
        cgrp_stmt_t *stmt;
        if (ALLOC_OBJ(stmt) == NULL) {
            OHM_ERROR("cgrp: failed to allocate statement");
            exit(1);
        }
        stmt->expr    = NULL;
        stmt->command = $1;

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
    ;

prop: TOKEN_ARG { $$ = CGRP_PROP_ARG($1.value); }
    ;

value: TOKEN_STRING {
        $$.type = CGRP_VALUE_TYPE_STRING;
	$$.str  = $1.value;
    }
    | TOKEN_IDENT {
        $$.type = CGRP_VALUE_TYPE_STRING;
	$$.str  = $1.value;
    }
    ;

command: command_group { $$ = $1; }
    | command_ignore   { $$ = $1; }
    ;

command_group: TOKEN_KW_GROUP group_name {
        cgrp_group_t *group;
        if ((group = group_find(ctx, $2.value)) == NULL) {
            OHM_ERROR("cgrp: reference to nonexisting group \"%s\"", $2.value);
            exit(1);
        }
        $$.group.type  = CGRP_CMD_GROUP;
	$$.group.group = group;
    }
    ;

group_name: TOKEN_STRING { $$ = $1; }
    | TOKEN_IDENT        { $$ = $1; }
    ;

command_ignore: TOKEN_KW_IGNORE { $$.ignore.type = CGRP_CMD_IGNORE; }
    ;

%%

/********************
 * config_parse
 ********************/
int
config_parse(cgrp_context_t *ctx, const char *path)
{
    FILE *fp;
    int   status;

    if ((fp = fopen(path, "r")) == NULL) {
        OHM_ERROR("cgrp: failed to open \"%s\" for reading", path);
	return FALSE;
    }

    lexer_init(fp);
    status = yyparse(ctx);
    lexer_exit();
    fclose(fp);

    return status == 0;
}


/********************
 * config_dump
 ********************/
void
config_print(cgrp_context_t *ctx, FILE *fp)
{
    partition_dump(ctx, fp);
    group_dump(ctx, fp);
    procdef_dump(ctx, fp);
}


void
yyerror(cgrp_context_t *ctx, const char *msg)
{
    (void)ctx;

    OHM_ERROR("parse error: %s\n", msg);
    exit(1);                              /* XXX would be better not to */
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
