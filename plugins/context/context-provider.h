#ifndef POLICY_CONTEXT_PROVIDER_H
#define POLICY_CONTEXT_PROVIDER_H
#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-fact.h>

extern int DBG_CONTEXT;

typedef struct context_key_value_pair_s context_key_value_pair_t;
typedef struct context_info_s context_info_t;

struct context_key_value_pair_s {
    char *key;
    union {
	char *s;
	int i;
    }value;
    int type;
    int have_kvp;
    context_key_value_pair_t *next;
};

struct context_info_s {
    char *fact;
    char *field;
    int field_type;
    char *cf_key;
    int has_subscribers;
    context_key_value_pair_t *kvp;
    context_info_t *next;
};

typedef struct context_match_s {
    context_info_t *info;
    union {
	char *s;
	int i;
    }value;
    int have_value;
    int complete;
    int mismatch;
} context_match_t;

context_info_t * parse_value(const char *s);

#define PLUGIN_PREFIX  context
#define PLUGIN_NAME    "context"
#define PLUGIN_VERSION "0.0.1"

#define CF_BUS_NAME "com.nokia.policy.context"

#endif
