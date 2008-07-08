/**
 * @file signaling.h
 * @brief OHM signaling plugin 
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#ifndef SIGNALING_H
#define SIGNALING_H

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <dbus/dbus.h>

#include "signaling_marshal.h"

#include <dres/dres.h>
#include <dres/variables.h>
#include <ohm/ohm-fact.h>

#define DBUS_INTERFACE_POLICY    "com.nokia.policy"
#define DBUS_INTERFACE_FDO       "org.freedesktop.DBus"
#define DBUS_PATH_POLICY         "/com/nokia/policy"

#define METHOD_POLICY_REGISTER    "register"
#define METHOD_POLICY_UNREGISTER  "unregister"
#define SIGNAL_POLICY_ACK         "status"
#define SIGNAL_NAME_OWNER_CHANGED "NameOwnerChanged"


#define TRANSACTION_TYPE (transaction_get_type())
#define TRANSACTION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), TRANSACTION_TYPE, Transaction))
#define TRANSACTION_CLASS(vtable) (G_TYPE_CHECK_CLASS_CAST((vtable), TRANSACTION_TYPE, TransactionClass))
#define IS_TRANSACTION (obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TRANSACTION_TYPE))
#define IS_TRANSACTION_CLASS (vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), TRANSACTION_TYPE))
#define TRANSACTION_GET_CLASS(inst) (G_TYPE_INSTANCE_GET_CLASS((inst), TRANSACTION_TYPE, TransactionClass))


/*
 * The enforcement point interface 
 */

#define EP_STRATEGY_TYPE (enforcement_point_get_type())
#define EP_STRATEGY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), EP_STRATEGY_TYPE, EnforcementPointStrategy))
#define IS_EP_STRATEGY (obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EP_STRATEGY_TYPE))
#define EP_STRATEGY_GET_INTERFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE((inst), EP_STRATEGY_TYPE, EnforcementPointInterface))

/*
 * ExternalEPStrategy 
 */

#define EXTERNAL_EP_STRATEGY_TYPE (external_ep_get_type())
#define EXTERNAL_EP_STRATEGY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXTERNAL_EP_STRATEGY_TYPE, ExternalEPStrategy))
#define EXTERNAL_EP_STRATEGY_CLASS(vtable) (G_TYPE_CHECK_CLASS_CAST((vtable), EXTERNAL_EP_STRATEGY_TYPE, ExternalEPStrategyClass))
#define IS_EXTERNAL_EP_STRATEGY (obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXTRERNAL_EP_STRATEGY_TYPE))
#define IS_EXTERNAL_EP_STRATEGY_CLASS (vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), EXTRERNAL_EP_STRATEGY_TYPE))
#define EXTERNAL_EP_STRATEGY_GET_CLASS(inst) (G_TYPE_INSTANCE_GET_CLASS((inst), EXTERNAL_EP_STRATEGY_TYPE, ExternalEPStrategyClass))

/*
 * InternalEPStrategy 
 */

#define INTERNAL_EP_STRATEGY_TYPE (internal_ep_get_type())
#define INTERNAL_EP_STRATEGY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), INTERNAL_EP_STRATEGY_TYPE, InternalEPStrategy))
#define INTERNAL_EP_STRATEGY_CLASS(vtable) (G_TYPE_CHECK_CLASS_CAST((vtable), INTERNAL_EP_STRATEGY_TYPE, InternalEPStrategyClass))
#define IS_INTERNAL_EP_STRATEGY (obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), INTRERNAL_EP_STRATEGY_TYPE))
#define IS_INTERNAL_EP_STRATEGY_CLASS (vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), INTRERNAL_EP_STRATEGY_TYPE))
#define INTERNAL_EP_STRATEGY_GET_CLASS(inst) (G_TYPE_INSTANCE_GET_CLASS((inst), INTERNAL_EP_STRATEGY_TYPE, InternalEPStrategyClass))

typedef struct _Transaction {
    GObject         parent;
    guint           txid;
    GSList         *acked;
    GSList         *nacked;
    GSList         *not_answered;
    guint           timeout; /* in milliseconds */
    guint           timeout_id; /* g_source */
    gboolean        built_ready;
    GSList         *facts;

} Transaction;

typedef struct _TransactionClass {
    GObjectClass    parent;
} TransactionClass;


/* Enforcement points */

typedef struct _EnforcementPoint EnforcementPoint;

typedef struct _EnforcementPointInterface {
    GTypeInterface  parent;
    gboolean        (*receive_ack) (EnforcementPoint * self, Transaction *transaction, guint status);
	gboolean        (*stop_transaction) (EnforcementPoint * self, Transaction *transaction);
	gboolean        (*unregister) (EnforcementPoint * self);
	gboolean        (*send_decision) (EnforcementPoint * self, Transaction *transaction);
} EnforcementPointInterface;

GType           enforcement_point_get_type(void);
gboolean        enforcement_point_receive_ack (EnforcementPoint * self, Transaction *transaction, guint status);
gboolean        enforcement_point_send_decision(EnforcementPoint * self, Transaction *transaction);
gboolean        enforcement_point_stop_transaction(EnforcementPoint * self, Transaction *transaction);
gboolean        enforcement_point_unregister(EnforcementPoint * self);

GType           transaction_get_type(void);
gboolean        transaction_done(Transaction *t);
void            transaction_complete(Transaction *t);
void            transaction_add_ep(Transaction *t, EnforcementPoint *ep);
void            transaction_remove_ep(Transaction *t, EnforcementPoint *ep);
void            transaction_ack_ep(Transaction *t, EnforcementPoint *ep, gboolean ack);

typedef struct _fact {
    gchar *key;
    GSList *values;
} fact;

/*
 * The implemented strategies 
 */

/*
 * ExternalEPStrategy 
 */

typedef struct _ExternalEPStrategy {
    GObject         parent;
    gchar          *id;
    /* DBusConnection *c; */
    GSList         *ongoing_transactions;

} ExternalEPStrategy;

typedef struct _ExternalEPStrategyClass {
    GObjectClass    parent;
    GSList *pending_signals;
} ExternalEPStrategyClass;

typedef struct _pending_signal {
    GSList *facts;
    Transaction *transaction;
    ExternalEPStrategyClass *klass;
} pending_signal;

GType           external_ep_get_type(void);

/*
 * InternalEPStrategy 
 */

typedef struct _InternalEPStrategy {
    GObject         parent;
    gchar          *id;
    GSList         *ongoing_transactions;

} InternalEPStrategy;

typedef struct _InternalEPStrategyClass {
    GObjectClass    parent;
} InternalEPStrategyClass;

GType           internal_ep_get_type(void);


/* API functions */

EnforcementPoint * register_enforcement_point(const gchar * uri, gboolean internal);

gboolean unregister_enforcement_point(const gchar *uri);

Transaction * queue_decision(GSList *facts, int txid, gboolean need_transaction, guint timeout);

gboolean init_signaling();

gboolean deinit_signaling();

DBusHandlerResult dbus_ack(DBusConnection * c, DBusMessage * msg, void *data);

DBusHandlerResult register_external_enforcement_point(DBusConnection * c, DBusMessage * msg,
        void *user_data);

DBusHandlerResult unregister_external_enforcement_point(DBusConnection * c, DBusMessage * msg, void *user_data);

DBusHandlerResult update_external_enforcement_points(DBusConnection * c, DBusMessage * msg, void *user_data);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
