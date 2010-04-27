#include <context_provider.h>
#include <stdlib.h>
#include "context-provider.h"

context_info_t *context_info;

int DBG_CONTEXT;

OHM_DEBUG_PLUGIN(context,
    OHM_DEBUG_FLAG("context", "policy CF facts", &DBG_CONTEXT)
);

static void register_fact(context_info_t *info, OhmFactStore *fact_store);
static void fact_updated_cb(void *data, OhmFact *fact, GQuark field_quark, gpointer value);
static void save_field_value(GValue *value, context_match_t *match);
static context_info_t * find_matching_fact(OhmFact *fact);
static int check_if_fact_matches_kvp(context_key_value_pair_t *kvp, OhmFact *fact);
static void export_fact_to_cf(context_match_t *match);
void cf_subscription_changed_cb(int subscribed, void* user_data);

/********************
 * plugin_init
 ********************/
static void
plugin_init(OhmPlugin *plugin)
{
    OhmFactStore *fact_store;
    if (!OHM_DEBUG_INIT(context)) {
        OHM_WARNING("context provider: failed to register for debugging");
    }

    fact_store = ohm_get_fact_store();
    if (fact_store == NULL) {
	OHM_ERROR("context provider: failed to initalize factstore");
	exit(1);
    }

    /* CF */
    context_provider_init(DBUS_BUS_SYSTEM, "com.nokia.policy.context");

    const char * facts_csv;
    facts_csv = (const char *)ohm_plugin_get_param(plugin, "facts");
    if (facts_csv == NULL) {
	return;
    }
    context_info = parse_value(facts_csv);
    {
	context_info_t *info;
	if (context_info == NULL) {
	    OHM_ERROR("No context variables!!!");
	}
	for (info = context_info; info != NULL; info = info->next) {
	    context_key_value_pair_t *kvp;
	    OHM_INFO("context provider: fact: %s", info->fact);
	    OHM_INFO("context provider: field: %s", info->field);
	    OHM_INFO("context provider: cf-key: %s", info->cf_key);
	    OHM_INFO("context provider: next: %p", info->next);

	    for (kvp = info->kvp; kvp != NULL; kvp = kvp->next) {
		switch (kvp->type) {
		case 's':
		    OHM_INFO("context provider: \t%s: '%s'", kvp->key, kvp->value.s);
		    break;
		case 'i':
		    OHM_INFO("context provider: \t%s: %d", kvp->key, kvp->value.i);
		    break;
		default:
		    OHM_ERROR("context provider: \tUNUPPORTED TYPE %c: %s: 0x%x", kvp->type, kvp->key, kvp->value.i);
		    break;
		}
	    }
	    register_fact(info, fact_store);
	}
    }
}


static void register_fact(context_info_t *info, OhmFactStore *fact_store)
{
    GSList  *list;
    OhmFact *fact;

    OHM_INFO("%s:%d %s(): registering fact '%s'", __FILE__, __LINE__, __FUNCTION__, info->fact);

    list = ohm_fact_store_get_facts_by_name(fact_store, info->fact);
    for (;list != NULL; list  = g_slist_next(list)) {
	GValue *value;
	fact = (OhmFact *)list->data;
	value = ohm_fact_get(fact, info->field);
	if(value == NULL) {
	    /* fact doesn't have given field, mismatch! */
	    OHM_ERROR("context provider: fact %s doesn't contain a field %s, ignoring it", info->fact, info->field);
	    return;
	}

	if(check_if_fact_matches_kvp(info->kvp, fact)) {
	    context_match_t match;

	    OHM_INFO("%s:%d %s(): We have a match for %s!", __FILE__, __LINE__, __FUNCTION__, info->fact);

	    match.info = info;
	    match.have_value=1;
	    match.complete=1;
	    save_field_value(value, &match);
	    info->has_subscribers=1;

	    OHM_INFO("context_provider_install_key(%s, %d, %p, %p);", info->cf_key, 0, cf_subscription_changed_cb, info);
	    context_provider_install_key(info->cf_key, 0, cf_subscription_changed_cb, info);
	    export_fact_to_cf(&match);
	    /* subscribe to all facts given by the context_info list */
	    /*	g_signal_connect(fact_store_pointer, "inserted", G_CALLBACK(fact_inserted_cb), NULL);
	    g_signal_connect(fact_store_pointer, "removed" , G_CALLBACK(fact_removed_cb), NULL);*/
	    g_signal_connect(G_OBJECT(fact_store), "updated" , G_CALLBACK(fact_updated_cb), NULL);
	    info->has_subscribers=0;
	    break;
	}
    }

}

void cf_subscription_changed_cb(int subscribed, void* data)
{
    context_info_t *info = (context_info_t *)data;

    OHM_DEBUG(DBG_CONTEXT, "context provider: %s(%d, %p)", __FUNCTION__, subscribed, data);
    if (info == NULL) {
	return;
    }

    OHM_DEBUG(DBG_CONTEXT, "context provider: %s(%d, %s)", __FUNCTION__, subscribed, info->cf_key);

    if (subscribed) {
	info->has_subscribers = 1;
    }
    else {
	info->has_subscribers = 0;
    }
}

static void fact_updated_cb(void *data, OhmFact *fact, GQuark field_quark, gpointer value)
{
    context_match_t match;
    const char *field_name=NULL;
    GValue *gval = (GValue *)value;
    (void) data;

    if (fact == NULL) {
        OHM_ERROR("context provider: %s() called with null fact pointer",__FUNCTION__);
        return;
    }
    if (value == NULL) {
	OHM_DEBUG(DBG_CONTEXT, "context provider: %s() - ignoring NULL value", __FUNCTION__);
	return;
    }

    /* first find which fact changed */
    field_name = g_quark_to_string(field_quark);

    memset(&match, 0, sizeof(context_match_t));

    /* check if it is a fact we are interested in */
    match.info = find_matching_fact(fact);
    if (match.info != NULL) {
	/* yes, is the field we are looking for? */
	if (strcmp(field_name, match.info->field) == 0) {
	    /* check if our kvps match */
	    save_field_value(gval, &match);
	    export_fact_to_cf(&match);
	}
    }
}


static context_info_t * find_matching_fact(OhmFact *fact)
{
    const char *fact_name;
    context_info_t *info;

    fact_name = ohm_structure_get_name(OHM_STRUCTURE(fact));
    if (fact_name == NULL) {
	OHM_ERROR("context provider: WTF? Nameless fact update? o_O");
	return NULL;
    }
    for (info = context_info; info != NULL; info = info->next) {
	/* is it something we are interested in? */
	if (strcmp(fact_name, info->fact) == 0) {
	    /* facts match, now check the kvps */
	    if (check_if_fact_matches_kvp(info->kvp, fact)) {
		OHM_DEBUG(DBG_CONTEXT, "context provider: found matching fact: %s{%s}=>%s", info->fact, info->field, info->cf_key);
		return info;
	    }
	}
    }
    return NULL;
}


static int check_if_fact_matches_kvp(context_key_value_pair_t *kvp, OhmFact *fact)
{
    for (; kvp != NULL; kvp = kvp->next) {
	int kvp_match = 0;
	GValue *field_value = ohm_fact_get(fact, kvp->key);
	if(field_value == NULL) {
	    /* fact doesn't have given field, mismatch! */
	    OHM_WARNING("context provider: fact doesn't contain a field %s", kvp->key);
	    return 0;
	}
	/* see if the keys match */
	switch (G_VALUE_TYPE(field_value)) {
	case G_TYPE_STRING:
	    if (strcmp(g_value_get_string(field_value), kvp->value.s) == 0) {
		kvp_match = 1;
		OHM_DEBUG(DBG_CONTEXT, "context provider: found matching kvp %s:'%s'", kvp->key, kvp->value.s);
	    }
	    break;
	case G_TYPE_INT:
	    if (kvp->value.i == g_value_get_int(field_value)) {
		kvp_match = 1;
		OHM_DEBUG(DBG_CONTEXT, "context provider: found matching kvp %s:%d", kvp->key, kvp->value.i);
	    }
	    break;
	}
	if(kvp_match) {
	    /* yes, flag it */
	    kvp->have_kvp = 1;
	}
	else {
	    /* values don't match, we have a mismatch, ignore the fact */
	    return 0;
	}
    }
    return 1;
}

static void save_field_value(GValue *value, context_match_t *match)
{
    switch (G_VALUE_TYPE(value)) {
    case G_TYPE_STRING:
	match->value.s = strdup(g_value_get_string(value));
	OHM_DEBUG(DBG_CONTEXT, "context provider: saved field value as: %s", match->value.s);
	break;
    case G_TYPE_INT:
	match->value.i = g_value_get_int(value);
	OHM_DEBUG(DBG_CONTEXT, "context provider: saved field value as: %d", match->value.i);
	break;
    default:
	OHM_ERROR("resource: [%s] Unsupported data type (%d) for field '%s'",
		  __FUNCTION__, G_VALUE_TYPE(value), match->info->field);
	return;
    }
    match->have_value = 1;
}



static void export_fact_to_cf(context_match_t *match)
{
    OHM_DEBUG(DBG_CONTEXT, "context provider: exporting %s. %s %s subscribers", match->info->cf_key,
	     match->info->fact, match->info->has_subscribers?"has":"hasn't");
    if(match->info->has_subscribers) {
	switch(match->info->field_type) {
	case 's':
	    context_provider_set_string(match->info->cf_key, match->value.s);
	    OHM_DEBUG(DBG_CONTEXT, "context provider: exporting to CF %s: '%s'",match->info->cf_key, match->value.s);
	    break;
	case 'i':
	    context_provider_set_integer(match->info->cf_key, match->value.i);
	    OHM_DEBUG(DBG_CONTEXT, "context provider: exporting to CF %s: %d",match->info->cf_key, match->value.i);
	    break;
	default:
	    OHM_DEBUG(DBG_CONTEXT, "context provider: exporting to CF %s: 0x%x", match->info->cf_key, match->value.i);
	    break;
	}
    }
}

/********************
 * plugin_exit
 ********************/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;
    context_info_t *info;
    for (info = context_info;info != NULL; ) {
	context_info_t *tmp;
	context_key_value_pair_t *kvp;
	free(info->fact);
	free(info->field);
	free(info->cf_key);
	for (kvp = info->kvp; kvp != NULL; ) {
	    context_key_value_pair_t *tmp_kvp;
	    if (kvp->type == 's')
		free(kvp->value.s);
	    free(kvp->key);
	    tmp_kvp = kvp;
	    kvp = kvp->next;
	    free(tmp_kvp);
	}
	tmp = info;
	info = info->next;
	free(tmp);
    }
}


/*****************************************************************************
 *                           *** public plugin API ***                       *
 *****************************************************************************/

/*****************************************************************************
 *                            *** OHM plugin glue ***                        *
 *****************************************************************************/

OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME,
                       PLUGIN_VERSION,
                       "ext-wolf.2.bergenheim@nokia.com",
                       OHM_LICENSE_NON_FREE, /* OHM_LICENSE_LGPL */
                       plugin_init, plugin_exit, NULL);

OHM_IMPORTABLE(int, resolve, (char *goal, char **locals));

OHM_PLUGIN_REQUIRES_METHODS(context, 1, 
   OHM_IMPORT("dres.resolve", resolve)
);

/*
OHM_PLUGIN_PROVIDES_METHODS(PLUGIN_PREFIX, 1,
                            OHM_EXPORT(provide_info, "provide"));
*/
/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

