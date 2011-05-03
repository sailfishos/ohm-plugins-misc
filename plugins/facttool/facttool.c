/*************************************************************************
 * Copyright (C) 2010 Intel Corporation.
 *
 * These OHM Modules are free software; you can redistribute
 * it and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA.
 * *************************************************************************/
#include "facttool.h"


typedef union
{
   dbus_int16_t  i16;   
   dbus_uint16_t u16;   
   dbus_int32_t  i32;   
   dbus_uint32_t u32;   
   double dbl;          
   unsigned char byt;   
   char *str;           
} DBusBasicValue;


static int DBG_FACTTOOL;
static OhmFactStore *store;


OHM_DEBUG_PLUGIN(facttool,
	OHM_DEBUG_FLAG("facttool", "facttool module"          , &DBG_FACTTOOL)
);

static void plugin_init(OhmPlugin *plugin)
{
	(void)plugin;

	if (!OHM_DEBUG_INIT(facttool))
		OHM_WARNING("facttool: Failed to initialize facttool plugin debbuging.");

	if ((store = ohm_get_fact_store()) == NULL) {
		OHM_ERROR("facttool: Failed to initialize factstore.");
	}

	OHM_INFO("facttool: init done");
}


static void plugin_exit(OhmPlugin *plugin)
{
	(void)plugin;

	OHM_INFO("facttool: exit");
}

static int gval_match(GValue * a, GValue * b)

{
	int match = FALSE;

	if (G_VALUE_TYPE(a) == G_VALUE_TYPE(b)) {
		switch(G_VALUE_TYPE(a)) {
		case G_TYPE_STRING:
			match = !strcmp(g_value_get_string(a), g_value_get_string(b));
			break;
		case G_TYPE_INT:
			match = g_value_get_int(a) == g_value_get_int(b);
			break;
		case G_TYPE_DOUBLE:
			match = g_value_get_double(a) == g_value_get_double(b);
			break;
		default:
			match = FALSE;
			break;
		}
	}

	return match;
}

static GValue * dbus_value_to_gvalue(DBusBasicValue * dbus_value, int dbus_value_type)
{
	GValue * gval;
	
	switch(dbus_value_type) {
	case DBUS_TYPE_STRING:
		gval = ohm_value_from_string(dbus_value->str);
		break;
	case DBUS_TYPE_BYTE:
		gval = ohm_value_from_int(dbus_value->byt);
		break;
	case DBUS_TYPE_INT16:
		gval = ohm_value_from_int(dbus_value->i16);
		break;
	case DBUS_TYPE_INT32:
		gval = ohm_value_from_int(dbus_value->i32);
		break;
	case DBUS_TYPE_DOUBLE:
		gval = ohm_value_from_double(dbus_value->dbl);
		break;
	default:
		gval = NULL;
	}
	return gval;
}

static int is_handled_type(int dbus_type)
{
	if ((dbus_type == DBUS_TYPE_STRING) ||
		(dbus_type == DBUS_TYPE_BYTE) ||
		(dbus_type == DBUS_TYPE_INT16) ||
		(dbus_type == DBUS_TYPE_INT32) ||
		(dbus_type == DBUS_TYPE_DOUBLE))
		return TRUE;
	else
		return FALSE;
}


static int map_to_dbus_type(GValue *gval, gchar *sig, void **value)
{
	int retval;
	gulong ul, *pul;
	glong l, *pl;
	gdouble d, *pd;

	*sig = '?';
	if (!G_IS_VALUE(gval)) {
		return DBUS_TYPE_INVALID;
	}

	switch(G_VALUE_TYPE(gval)) {
	case G_TYPE_STRING:
		*sig = 's';
		*value = (void *) g_strdup(g_value_get_string(gval));
		retval = DBUS_TYPE_STRING;
		break;
	case G_TYPE_INT:
		*sig = 'i';
		l = g_value_get_int(gval);
		pl = g_malloc(sizeof(glong));
		*pl = l;
		*value = pl;
		retval = DBUS_TYPE_INT32;
		break;
	case G_TYPE_UINT:
		*sig = 'u';
		ul = g_value_get_uint(gval);
		pul = g_malloc(sizeof(gulong));
		*pul = ul;
		*value = pul;
		retval = DBUS_TYPE_UINT32;
		break;
	case G_TYPE_LONG:
		*sig = 'i';
		l = g_value_get_long(gval);
		pl = g_malloc(sizeof(glong));
		*pl = l;
		*value = pl;
		retval = DBUS_TYPE_INT32;
		break;
	case G_TYPE_ULONG:
		*sig = 'u';
		ul = g_value_get_ulong(gval);
		pul = g_malloc(sizeof(gulong));
		*pul = ul;
		*value = pul;
		retval = DBUS_TYPE_UINT32;
		break;
	case G_TYPE_FLOAT:
		*sig = 'd';
		d = g_value_get_float(gval);
		pd = g_malloc(sizeof(gdouble));
		*pd = d;
		*value = pd;
		retval = DBUS_TYPE_DOUBLE;
		break;
	case G_TYPE_DOUBLE:
		*sig = 'd';
		d = g_value_get_double(gval);
		pd = g_malloc(sizeof(gdouble));
		*pd = d;
		*value = pd;
		retval = DBUS_TYPE_DOUBLE;
		break;
	default:
		retval = DBUS_TYPE_INVALID;
		break;
	}

	return retval;
}

/* Eat ssv from a dbus message, plus a(sv) if present
 * ssv: fact name, field name, value to set
 * a(sv): array of struct: field name-value to check
 * 	The aray is used to select the facts where the set operation should be applied
 */
static int setfact_single(DBusMessageIter * msg_it)

{
	const gchar 	*fact_name = NULL;
	const gchar	*field_name = NULL;
	int		n;
	DBusBasicValue	dbus_value;
	int		dbus_value_type;
	GValue      	*gval;
	GSList		*fact_list;
	GSList		*fact_list_it;
	OhmFact		*fact;

	/* Read name of the fact to set */
	if (dbus_message_iter_get_arg_type(msg_it) == DBUS_TYPE_STRING) {
		dbus_message_iter_get_basic(msg_it, (void *)&fact_name);
		dbus_message_iter_next(msg_it);
		fact_list = g_slist_copy(ohm_fact_store_get_facts_by_name(store, fact_name));
		n = fact_list ? g_slist_length(fact_list) : 0;
		OHM_DEBUG(DBG_FACTTOOL, "%s: %d facts found!", __FUNCTION__, n);
	} else {
		OHM_ERROR("%s:%d Invalid dbus request", __FUNCTION__, __LINE__);
		goto end;
	}
	/* read the field name */
	if (dbus_message_iter_get_arg_type(msg_it) == DBUS_TYPE_STRING) {
		dbus_message_iter_get_basic(msg_it, (void *)&field_name);
		dbus_message_iter_next(msg_it);
	} else {
		OHM_ERROR("%s:%d Invalid dbus request", __FUNCTION__, __LINE__);
		goto free_list;
	}
	/* Read the value to be set from a variant */
	if (dbus_message_iter_get_arg_type(msg_it) == DBUS_TYPE_VARIANT) {
		DBusMessageIter	variant_it;

		dbus_message_iter_recurse(msg_it, &variant_it);
		
		dbus_value_type = dbus_message_iter_get_arg_type(&variant_it);
		if (is_handled_type(dbus_value_type) == TRUE) {
			dbus_message_iter_get_basic(&variant_it, (void *)&dbus_value);
		} else {
			OHM_ERROR("%s:%d Invalid dbus request", __FUNCTION__, __LINE__);
			goto free_list;
		}
	} else {
		OHM_ERROR("%s:%d Invalid dbus request", __FUNCTION__, __LINE__);
		goto free_list;
	}
	dbus_message_iter_next(msg_it);

	/* Read optional array for fact selection: a(sv) */
	if (dbus_message_iter_get_arg_type(msg_it) == DBUS_TYPE_ARRAY) {
		DBusMessageIter	array_it;
		DBusMessageIter struct_it;
		DBusMessageIter variant_it;
		const gchar     *select_field = NULL;
		GValue          *select_gval;
		GValue          *fact_gval;
		DBusBasicValue  select_dbus_value;
		int             select_dbus_value_type;
		int             match;

		dbus_message_iter_recurse(msg_it, &array_it);
		while (dbus_message_iter_get_arg_type(&array_it) != DBUS_TYPE_INVALID) {
			if (dbus_message_iter_get_arg_type(&array_it) != DBUS_TYPE_STRUCT) {
				OHM_ERROR("%s:%d Invalid dbus request", __FUNCTION__, __LINE__);
				goto free_list;
			}
			dbus_message_iter_recurse(&array_it, &struct_it);
			if (dbus_message_iter_get_arg_type(&struct_it) == DBUS_TYPE_STRING) {
				dbus_message_iter_get_basic(&struct_it, (void *)&select_field);
				dbus_message_iter_next(&struct_it);
			} else {
				OHM_ERROR("%s:%d Invalid dbus request", __FUNCTION__, __LINE__);
				goto free_list;
			}
			if (dbus_message_iter_get_arg_type(&struct_it) == DBUS_TYPE_VARIANT) {
				dbus_message_iter_recurse(&struct_it, &variant_it);
				select_gval = NULL;
				select_dbus_value_type = dbus_message_iter_get_arg_type(&variant_it);
				if (is_handled_type(select_dbus_value_type) == TRUE) {
					dbus_message_iter_get_basic(&variant_it, (void *)&select_dbus_value);
					select_gval = dbus_value_to_gvalue(&select_dbus_value, select_dbus_value_type);
				}
				if (select_gval == NULL) {
					OHM_ERROR("%s:%d Invalid dbus request", __FUNCTION__, __LINE__);
					goto free_list;
				}
			}
			/* Filter the fact list using field:value */
			fact_list_it = fact_list;
			while (fact_list_it != NULL) {
				fact = (OhmFact *)fact_list_it->data;
				fact_gval = ohm_fact_get(fact, select_field);
				if (fact_gval == NULL)
					match = FALSE;
				else
					match = gval_match(fact_gval, select_gval);

				if (match == FALSE) {
					fact_list_it = fact_list = g_slist_remove(fact_list, fact);
					/* n is used only for debug/info log messages */
					n--;
				} else {
					fact_list_it = g_slist_next(fact_list_it);
				}
			}
			g_value_unset(select_gval);
			g_free(select_gval);

			dbus_message_iter_next(&array_it);
		}
		dbus_message_iter_next(msg_it);
	}
	/* Apply for all facts */
	fact_list_it = fact_list;
	while (fact_list_it != NULL) {

		fact = (OhmFact *)fact_list_it->data;
		gval = dbus_value_to_gvalue(&dbus_value, dbus_value_type);
		if (gval == NULL) {
			OHM_ERROR("%s: Unhandled value type %s", __FUNCTION__, dbus_message_type_to_string(dbus_value_type));
			goto free_list;
		}
		OHM_DEBUG(DBG_FACTTOOL, "%s: Setting one fact.", __FUNCTION__);
		ohm_fact_set(fact, field_name, gval);
		fact_list_it = g_slist_next(fact_list_it);
	}

	OHM_INFO("%s: %d facts %s updated on field %s", __FUNCTION__, n, fact_name, field_name);
	g_slist_free(fact_list);
	return 0;
free_list:
	g_slist_free(fact_list);
end:
	return -1;
}

/* Single/multiple set/create field of a fact or a collection of fact 
 * 
 * DBUS message format allowed:
 * ssv: fact name, field name, value to set
 * ssva(sv): fact name, field name, value to set, array of sv tupples to 
 * select facts on which the set operation must be applied. Each tupple
 * is a filter composed of a field name and a value.
 *
 * Multiple operation can be requested:
 * ssvssvssv: 3 fact operations
 * ssvssva(sv)ssv: 3 fact operations, the second one includes a selection
 * filter.
 *
 * If one error occurs, all operations are rolled back. If no error occurs,
 * all operations are committed.
 */
static DBusHandlerResult facttool_setfact(DBusConnection * c, DBusMessage * msg,
	void *user_data)

{
	DBusMessageIter msg_it;
	DBusMessage	*reply;
	int 		rollback = FALSE;
	
	dbus_message_iter_init(msg, &msg_it);

	ohm_fact_store_transaction_push(store);
	OHM_INFO("%s: Engaging fact operation", __FUNCTION__);
	/* Each element must be an array containing a fact operation description */
	while (dbus_message_iter_get_arg_type(&msg_it) != DBUS_TYPE_INVALID) {
		/* Execute the fact operation */
		if (setfact_single(&msg_it)) {
			OHM_ERROR("%s: failed to execute the fact operation", __FUNCTION__);
			rollback = TRUE;
			break;
		}
	}
	if (rollback == TRUE)
		OHM_INFO("%s: Rolling back fact operations", __FUNCTION__);
	else
		OHM_INFO("%s: Committing fact operations", __FUNCTION__);
	ohm_fact_store_transaction_pop(store, rollback);

	if ((reply = dbus_message_new_method_return(msg)) == NULL) {
		OHM_ERROR("%s: failed to allocate D-BUS reply", __FUNCTION__);
		goto end;
	}
	if (!dbus_connection_send(c, reply, NULL)) {
		OHM_ERROR("%s: failed to send the reply", __FUNCTION__);
	}
	dbus_message_unref(reply);
end:
	return DBUS_HANDLER_RESULT_HANDLED;
}

/* Get facts
 *
 * DBUS arguments:
 *
 * arg 0: string: name of the fact to be retrieved
 *
 * Returns a DBUS reply containing an array of facts, each one provided as an
 * array of struct including a string as the field name, and a variant for the
 * value associated to the field.
 *
 */
static DBusHandlerResult facttool_getfact(DBusConnection * c, DBusMessage * msg,
	void *user_data)

{
	const gchar 	*name = NULL;
	DBusMessageIter	rep_it;
	DBusMessageIter	fact_it;
	DBusMessageIter	fields_it;
	DBusMessage	*reply;
	int		n;
	GSList		*fact_list;
	DBusError	error;

	dbus_error_init(&error);

	/* Arg 0: String: name of the fact to get */
	if (dbus_message_get_args(msg, &error, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID)) {
		fact_list = ohm_fact_store_get_facts_by_name(store, name);
		n = fact_list ? g_slist_length(fact_list) : 0;
		OHM_DEBUG(DBG_FACTTOOL, "%s: %d facts found!", __FUNCTION__, n);
	} else {
		OHM_ERROR("%s: Invalid dbus request: %s", __FUNCTION__, error.message);
		goto cancel;
	}
	if ((reply = dbus_message_new_method_return(msg)) == NULL) {
		OHM_ERROR("%s: failed to allocate D-BUS reply", __FUNCTION__);
		goto cancel;
	}
	
	dbus_message_iter_init_append(reply, &rep_it);

	/* Open array of facts */
	if (!dbus_message_iter_open_container(&rep_it, DBUS_TYPE_ARRAY, "a(sv)", &fact_it)) {
		OHM_ERROR("%s: error opening container", __FUNCTION__);
		goto end;
	}
	while (fact_list != NULL) {
		GSList	*fields = NULL;
		OhmFact	*fact;

		if (!dbus_message_iter_open_container(&fact_it, DBUS_TYPE_ARRAY, "(sv)", &fields_it)) {
			OHM_ERROR("%s: error opening container", __FUNCTION__);
			goto end;
		}
		fact = (OhmFact *)fact_list->data;
		for (fields = ohm_fact_get_fields(fact); fields != NULL; fields = g_slist_next(fields)) {
			DBusMessageIter	struct_it;
			DBusMessageIter	variant_it;
			GQuark	qfield = (GQuark)GPOINTER_TO_INT(fields->data);
			const	gchar *field_name = g_quark_to_string(qfield);
			gchar	sig_c = '?';
			gchar	sig[2] = "?";
			void	*value;
			GValue	*gval = ohm_fact_get(fact, field_name);
			int	dbus_type = map_to_dbus_type(gval, &sig_c, &value);

			sig[0] = sig_c;
			if (dbus_type == DBUS_TYPE_INVALID) {
				OHM_WARNING("%s: ignoring invalid field %s", __FUNCTION__, field_name);
				continue;
			}
			if (!dbus_message_iter_open_container(&fields_it, DBUS_TYPE_STRUCT, NULL, &struct_it)) {
				OHM_ERROR("%s: error opening container", __FUNCTION__);
				g_free(value);
				goto end;
			}
			if (!dbus_message_iter_append_basic(&struct_it, DBUS_TYPE_STRING, &field_name)) {
				OHM_ERROR("%s: error appending OhmFact field", __FUNCTION__);
				g_free(value);
				goto end;
			}
			if (!dbus_message_iter_open_container(&struct_it, DBUS_TYPE_VARIANT, sig, &variant_it)) {
				OHM_ERROR("%s: error opening container", __FUNCTION__);
				g_free(value);
				goto end;
			}
			if (dbus_type == DBUS_TYPE_STRING) {
				if (!dbus_message_iter_append_basic(&variant_it, dbus_type, &value)) {
					OHM_ERROR("%s: error appending OhmFact value", __FUNCTION__);
					g_free(value);
					goto end;
				}
			} else {
				if (!dbus_message_iter_append_basic(&variant_it, dbus_type, value)) {
					OHM_ERROR("%s: error appending OhmFact value", __FUNCTION__);
					g_free(value);
					goto end;
				}
			}
			g_free(value);
			dbus_message_iter_close_container(&struct_it, &variant_it);
			dbus_message_iter_close_container(&fields_it, &struct_it);
		}

		dbus_message_iter_close_container(&fact_it, &fields_it);
		fact_list = g_slist_next(fact_list);
	}
	dbus_message_iter_close_container(&rep_it, &fact_it);

	if (!dbus_connection_send(c, reply, NULL)) {
		OHM_ERROR("%s: failed to send the reply", __FUNCTION__);
	}

end:
	dbus_message_unref(reply);
cancel:
	return DBUS_HANDLER_RESULT_HANDLED;
}

OHM_PLUGIN_DESCRIPTION("facttool",
                       "0.0.1",
                       "mathieux.soulard@intel.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_DBUS_METHODS(
	{NULL, DBUS_PATH_POLICY, METHOD_POLICY_FACTTOOL_SET_FACT,
		facttool_setfact, NULL},
	{NULL, DBUS_PATH_POLICY, METHOD_POLICY_FACTTOOL_GET_FACT,
		facttool_getfact, NULL}
);

