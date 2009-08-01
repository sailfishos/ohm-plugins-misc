#ifndef __OHM_PLUGIN_CGRP_H__
#define __OHM_PLUGIN_CGRP_H__

#include <stdio.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-fact.h>

#include <glib.h>

#include "cgrp-basic-types.h"
#include "mm.h"
#include "list.h"

#define PLUGIN_PREFIX   cgroups
#define PLUGIN_NAME    "cgroups"
#define PLUGIN_VERSION "0.0.1"

#define DEFAULT_CONFIG "/etc/ohm/plugins.d/syspart.conf"
#define DEFAULT_NOTIFY 3001


/*
 * a system partition
 */

typedef enum {
    CGRP_PARTFLAG_NONE     = 0x0,
    CGRP_PARTFLAG_NOFREEZE = 0x1,           /* partition not freezable */
} cgrp_part_flag_t;

typedef struct {
    char *name;                             /* name of this partition */
    char *path;                             /* path to this partition */
    int   flags;                            /* partition flags */
    int   tasks;                            /* task control */
    int   freeze;                           /* freeze control */
    int   cpu_shares;                       /* CPU share/weight control */
} cgrp_partition_t;


/*
 * a process classification group
 */

typedef enum {
    CGRP_GROUPFLAG_NONE   = 0x0,
    CGRP_GROUPFLAG_STATIC = 0x1,            /* statically partitioned group */
} cgrp_group_flag_t;

typedef struct {
    char             *name;                 /* group name */
    char             *description;          /* group description */
    int               flags;                /* group flags */
    list_hook_t       processes;            /* processes in this group */
    cgrp_partition_t *partition;            /* current partititon */
} cgrp_group_t;


/*
 * classification commands
 */

typedef enum {
    CGRP_CMD_UNKNOWN = 0,
    CGRP_CMD_GROUP,                         /* group classification */
    CGRP_CMD_IGNORE,                        /* ignore */
} cgrp_cmd_type_t;

#define CGRP_CMD_COMMON \
    cgrp_cmd_type_t type                    /* command type */

typedef struct {
    CGRP_CMD_COMMON;                        /* type, common fields */
} cgrp_cmd_any_t;

typedef struct {
    CGRP_CMD_COMMON;                        /* type, common fields */
    cgrp_group_t *group;                    /* classification group */
} cgrp_cmd_group_t;

typedef union {
    cgrp_cmd_any_t    any;                  /* any command */
    cgrp_cmd_group_t  group;                /* group classification command */
    cgrp_cmd_any_t    ignore;               /* ignore command */
} cgrp_cmd_t;


/*
 * classification statement expressions
 */

typedef enum {
    CGRP_VALUE_TYPE_UNKNOWN = 0,
    CGRP_VALUE_TYPE_STRING,
    CGRP_VALUE_TYPE_UINT32,
    CGRP_VALUE_TYPE_SINT32,
} cgrp_value_type_t;

typedef struct {
    cgrp_value_type_t type;
    union {
        char  *str;
        u32_t  u32;
        s32_t  s32;
    };
} cgrp_value_t;

typedef enum {
    CGRP_EXPR_UNKNOWN = 0,
    CGRP_EXPR_BOOL,
    CGRP_EXPR_PROP,
} cgrp_expr_type_t;

typedef union cgrp_expr_u cgrp_expr_t;

#define CGRP_EXPR_COMMON \
    cgrp_expr_type_t type


typedef enum {
    CGRP_BOOL_UNKNOWN = 0,
    CGRP_BOOL_AND,
    CGRP_BOOL_OR,
    CGRP_BOOL_NOT
} cgrp_bool_op_t;

typedef struct {
    CGRP_EXPR_COMMON;
    cgrp_bool_op_t  op;
    cgrp_expr_t    *arg1;
    cgrp_expr_t    *arg2;
} cgrp_bool_expr_t;

typedef enum {
    CGRP_OP_UNKNOWN = 0,
    CGRP_OP_EQUAL,
    CGRP_OP_NOTEQ
} cgrp_prop_op_t;

#define CGRP_MAX_ARGS     32                /* max command line argument */
#define CGRP_AVG_ARG      64                /* average argument length */
#define CGRP_MAX_CMDLINE (CGRP_MAX_ARGS * CGRP_AVG_ARG)

typedef enum {
    CGRP_PROP_BINARY,
    CGRP_PROP_ARG0,
    /* CGRP_PROP_ARG(1) ... CGRP_PROP_ARG(CGRP_MAX_ARGS - 1) */
    CGRP_PROP_ARG_MAX = CGRP_PROP_ARG0 + CGRP_MAX_ARGS - 1,
    CGRP_PROP_CMDLINE,
    CGRP_PROP_TYPE,
    CGRP_PROP_PARENT,
    CGRP_PROP_EUID,
    CGRP_PROP_EGID,
} cgrp_prop_type_t;

#define CGRP_PROP_ARG(n) ((cgrp_prop_type_t)(CGRP_PROP_ARG0 + (n)))

typedef struct {
    CGRP_EXPR_COMMON;
    cgrp_prop_type_t prop;
    cgrp_prop_op_t   op;
    cgrp_value_t     value;
} cgrp_prop_expr_t;


union cgrp_expr_u {
    cgrp_expr_type_t type;                  /* expression type */
    cgrp_bool_expr_t bool;                  /* boolean expression */
    cgrp_prop_expr_t prop;                  /* property expression */
};
    

/*
 * classification statements (conditional commands)
 */

typedef struct cgrp_stmt_s cgrp_stmt_t;
struct cgrp_stmt_s {
    cgrp_expr_t   *expr;                    /* test expression */
    cgrp_cmd_t     command;                 /* command if expr is true */
    cgrp_stmt_t   *next;                    /* more statements */
};


/*
 * a process definition
 */

typedef struct {
    char          *binary;                  /* path to binary */
    cgrp_stmt_t   *statements;              /* classification statements */
} cgrp_procdef_t;


/*
 * a classified process
 */

typedef struct {
    pid_t         pid;                      /* process id */
    char         *binary;                   /* path to binary */
    cgrp_group_t *group;                    /* current group */
    list_hook_t   proc_hook;                /* hook to process table */
    list_hook_t   group_hook;               /* hook to group */
} cgrp_process_t;

typedef enum {
    CGRP_PROC_BINARY = 0,                   /* process binary path */
    CGRP_PROC_ARG0   = CGRP_PROP_ARG0,      /* process arguments */
    /* CGRP_PROC_ARG(1) ... CGRP_PROC_ARG(CGRP_MAX_ARGS - 1) */
    CPGR_PROC_ARG_MAX = CGRP_PROC_ARG0 + CGRP_MAX_ARGS - 1,
    CGRP_PROC_CMDLINE,                      /* process command line */
    CGRP_PROC_TYPE,                         /* process type */
    CGRP_PROC_PPID,                         /* process parent binary */
    CGRP_PROC_EUID,                         /* effective user ID */
    CGRP_PROC_EGID,                         /* effective group ID */
} cgrp_proc_attr_type_t;

#define CGRP_PROC_ARG(n) ((cgrp_proc_attr_type_t)(CGRP_PROC_ARG0 + (n)))

typedef enum {
    CGRP_PROC_UNKNOWN = 0,
    CGRP_PROC_USER,
    CGRP_PROC_KERNEL,
} cgrp_proc_type_t;


typedef struct {
    u64_t              mask;                /* attribute mask */
    pid_t              pid;                 /* process id */
    pid_t              ppid;                /* parent process id */
    char              *binary;              /* path to binary */
    cgrp_proc_type_t   type;                /* user or kernel process */
    char              *cmdline;             /* command line */
    char             **argv;                /* command line arguments */
    int                argc;                /* number of arguments */
    uid_t              euid;                /* effective user id */
    gid_t              egid;                /* effective group id */
} cgrp_proc_attr_t;


/*
 * system partitioning context
 */

typedef struct {
    cgrp_partition_t *partitions;           /* system partitions */
    int               npartition;           /* number of partitions */
    cgrp_partition_t *root;                 /* root partition */
    cgrp_group_t     *groups;               /* classification groups */
    int               ngroup;               /* number of groups */
    cgrp_procdef_t   *procdefs;             /* process definitions */
    int               nprocdef;             /* number of process definitions */
    cgrp_procdef_t   *fallback;             /* fallback process definition */

    GHashTable       *ruletbl;              /* lookup table of procdefs */
    GHashTable       *grouptbl;             /* lookup table of groups */
    GHashTable       *parttbl;              /* lookup table of partitions */
    list_hook_t      *proctbl;              /* lookup table of processes */
    
    OhmFactStore     *store;                /* ohm factstore */
    GObject          *sigconn;              /* policy signaling interface */
    gulong            sigdcn;               /* policy decision id */
    gulong            sigkey;               /* policy keychange id */
    
    int               notifsock;            /* notification fd */
    GIOChannel       *notifchnl;
    guint             notifsrc;
    int             (*resolve)(char *, char **);

} cgrp_context_t;


/* cgrp-plugin.c */
extern int DBG_EVENT, DBG_PROCESS, DBG_CLASSIFY, DBG_ACTION;

/* cgrp-process.c */
int  proc_init(cgrp_context_t *);
void proc_exit(cgrp_context_t *);

char   *process_get_binary (cgrp_proc_attr_t *);
char   *process_get_cmdline(cgrp_proc_attr_t *);
char  **process_get_argv   (cgrp_proc_attr_t *);
uid_t   process_get_euid   (cgrp_proc_attr_t *);
gid_t   process_get_egid   (cgrp_proc_attr_t *);
pid_t   process_get_ppid   (cgrp_proc_attr_t *);

cgrp_proc_type_t process_get_type(cgrp_proc_attr_t *);

int process_set_group(cgrp_context_t *, cgrp_process_t *, cgrp_group_t *);
int process_clear_group(cgrp_process_t *);
int process_ignore(cgrp_context_t *, cgrp_process_t *);
int process_remove_by_pid(cgrp_context_t *, pid_t);
int process_scan_proc(cgrp_context_t *);



/* cgrp-partition.c */
int  partition_init(cgrp_context_t *);
void partition_exit(cgrp_context_t *);
int  partition_config(cgrp_context_t *);

cgrp_partition_t *partition_add(cgrp_context_t *, cgrp_partition_t *);
int               partition_open(cgrp_partition_t *, const char *,const char *);
void              partition_close(cgrp_partition_t *);
cgrp_partition_t *partition_find(cgrp_context_t *, const char *);
cgrp_partition_t *partition_lookup(cgrp_context_t *, const char *);

void partition_dump(cgrp_context_t *, FILE *);
void partition_print(cgrp_context_t *, cgrp_partition_t *, FILE *);
int partition_process(cgrp_partition_t *, pid_t);
int partition_group(cgrp_partition_t *, cgrp_group_t *);
int partition_freeze(cgrp_partition_t *, int);
int partition_set_cpu_share(cgrp_partition_t *, unsigned int);

/* cgrp-group.c */
int  group_init(cgrp_context_t *);
void group_exit(cgrp_context_t *);
int  group_config(cgrp_context_t *);

cgrp_group_t *group_add(cgrp_context_t *, cgrp_group_t *);
void          group_purge(cgrp_group_t *);
cgrp_group_t *group_find(cgrp_context_t *, const char *);
cgrp_group_t *group_lookup(cgrp_context_t *, const char *);

void group_dump(cgrp_context_t *, FILE *);
void group_print(cgrp_context_t *, cgrp_group_t *, FILE *);

/* cgrp_procdef.c */
int  procdef_init(cgrp_context_t *);
void procdef_exit(cgrp_context_t *);

cgrp_procdef_t *procdef_add(cgrp_context_t *, cgrp_procdef_t *);
void            procdef_purge(cgrp_procdef_t *);

void procdef_dump(cgrp_context_t *, FILE *);
void procdef_print(cgrp_context_t *, cgrp_procdef_t *, FILE *);
cgrp_cmd_t *rule_eval(cgrp_procdef_t *, cgrp_proc_attr_t *);


/* cgrp-classify.c */
int  classify_init  (cgrp_context_t *);
void classify_exit  (cgrp_context_t *);
int  classify_config(cgrp_context_t *);
int  classify_process(cgrp_context_t *, pid_t);


/* cgrp-ep.c */
int  ep_init(cgrp_context_t *, GObject *(*)(gchar *));
void ep_exit(cgrp_context_t *, gboolean (*)(GObject *));



/* cgrp-hash.c */
int  rule_hash_init  (cgrp_context_t *);
void rule_hash_exit  (cgrp_context_t *);
int  rule_hash_insert(cgrp_context_t *, cgrp_procdef_t *);
int  rule_hash_remove(cgrp_context_t *, const char *);
cgrp_procdef_t *rule_hash_lookup(cgrp_context_t *, const char *);

int  proc_hash_init  (cgrp_context_t *);
void proc_hash_exit  (cgrp_context_t *);
int  proc_hash_insert(cgrp_context_t *, cgrp_process_t *);
cgrp_process_t *proc_hash_remove(cgrp_context_t *, pid_t);
void proc_hash_unhash(cgrp_context_t *, cgrp_process_t *);
cgrp_process_t *proc_hash_lookup(cgrp_context_t *, pid_t);

int  group_hash_init  (cgrp_context_t *);
void group_hash_exit  (cgrp_context_t *);
int  group_hash_insert(cgrp_context_t *, cgrp_group_t *);
int  group_hash_delete(cgrp_context_t *, const char *);
cgrp_group_t *group_hash_lookup(cgrp_context_t *, const char *);

int  part_hash_init  (cgrp_context_t *);
void part_hash_exit  (cgrp_context_t *);
int  part_hash_insert(cgrp_context_t *, cgrp_partition_t *);
int  part_hash_delete(cgrp_context_t *, const char *);
cgrp_partition_t *part_hash_lookup(cgrp_context_t *, const char *);



/* cgrp-eval.c */
cgrp_expr_t *bool_expr(cgrp_bool_op_t, cgrp_expr_t *, cgrp_expr_t *);
cgrp_expr_t *prop_expr(cgrp_prop_type_t, cgrp_prop_op_t, cgrp_value_t *);
void statement_free_all(cgrp_stmt_t *);

void statements_print(cgrp_context_t *, cgrp_stmt_t *, FILE *);
void statement_print(cgrp_context_t *, cgrp_stmt_t *, FILE *);
void expr_print(cgrp_context_t *, cgrp_expr_t *, FILE *);
void bool_print(cgrp_context_t *, cgrp_bool_expr_t *, FILE *);
void prop_print(cgrp_context_t *, cgrp_prop_expr_t *, FILE *);
void value_print(cgrp_context_t *, cgrp_value_t *, FILE *);
void command_print(cgrp_context_t *, cgrp_cmd_t *, FILE *);
int  command_execute(cgrp_context_t *, cgrp_process_t *, cgrp_cmd_t *);
int  expr_eval(cgrp_expr_t *expr, cgrp_proc_attr_t *);


/* cgrp-config.y */
int  config_parse(cgrp_context_t *, const char *);
void config_print(cgrp_context_t *, FILE *);

/* cgrp-lexer.l */
int  lexer_init(FILE *);
void lexer_exit(void);

/* cgrp-utils.h */
uid_t cgrp_getuid(const char *);
gid_t cgrp_getgid(const char *);

/* cgrp-notify.c */
int  notify_init(cgrp_context_t *, int);
void notify_exit(cgrp_context_t *);

#endif /* __OHM_PLUGIN_DBUS_H__ */




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
