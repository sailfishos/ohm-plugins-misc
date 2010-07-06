/*************************************************************************
Copyright (C) 2010 Nokia Corporation.

These OHM Modules are free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


static void condition_cb(LibHalContext *, const char *, const char *,
                         const char *);

static button_ev_t *event_init(void)
{
    button_ev_t     *ev   = NULL;
    DBusConnection  *conn = NULL;
    LibHalContext   *ctx  = NULL;
    DBusError        err;

    dbus_error_init(&err);
        
    do { /* not a loop */
        if ((ev = malloc(sizeof(*ev))) == NULL) {
            OHM_ERROR("buttons: Can't get memory for button_ev_t");
            break;
        }

        if ((conn = ohm_plugin_dbus_get_connection()) == NULL) {
            OHM_ERROR("buttons: D-Bus initialization error");
            break;
        }

        if ((ctx  = libhal_ctx_new()) == NULL                   ||
            !libhal_ctx_set_dbus_connection(ctx, conn)          ||
            !libhal_ctx_set_user_data(ctx, ev)                  ||
            !libhal_ctx_set_device_condition(ctx, condition_cb) ||
            !libhal_ctx_init(ctx, &err)                           ) {
            OHM_ERROR("buttons: HAL context initialization failed");
            break;
        }

        if (!libhal_device_add_property_watch(ctx, BUTTONS_POWER_UDI, &err)) {
            OHM_ERROR("buttons: failed to add property watch to power button");
            break;
        }

        memset(ev, 0, sizeof(*ev));
        ev->conn  = conn;
        ev->ctx   = ctx;

        return ev;

    } while(0);

    if (dbus_error_is_set(&err)) {
        OHM_ERROR("buttons: D-Bus error: %s", err.message);
        dbus_error_free(&err);
    }

    if (ctx != NULL)
        libhal_ctx_free(ctx);

    free(ev);

    return NULL;
}

static void event_exit(button_ev_t *ev)
{
    if (ev != NULL) {
        libhal_device_remove_property_watch(ev->ctx, BUTTONS_POWER_UDI, NULL);
        libhal_ctx_shutdown(ev->ctx, NULL);
        libhal_ctx_free(ev->ctx);
        free(ev);
    }
}


static void condition_cb(LibHalContext *ctx, const char *udi,
                         const char *name, const char *detail)
{
    button_ev_t *ev = libhal_ctx_get_user_data(ctx);

    OHM_DEBUG(DBG_EVENT, "%s(%s, %s, %s)", __FUNCTION__, udi, name, detail);

    if (ev != NULL) {
        if (!strcmp(udi, BUTTONS_POWER_UDI) &&
            !strcmp(name, "ButtonPressed")  &&
            !strcmp(detail, "power")           )
        {
            dresif_power_key_pressed();
        }
    }
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
