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

/* This file contains the logic for following HAL events that originate
 * from the wired headset. */

#include "accessories.h"

static gchar *token = "button";
static int DBG_HEADSET;

gboolean complete_headset_cb (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data)
{
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    GValue *capabilities = NULL;
    gboolean found = FALSE, had_mic = FALSE, had_phones = FALSE, had_set = FALSE, had_line_out = FALSE, had_video_out = FALSE;
    gboolean video_changed, line_changed, headset_changed;
    GSList *list = NULL, *i;
    gchar *fact_name = "com.nokia.policy.audio_device_accessible";

    (void) capability;
    (void) added;
    (void) removed;
    (void) user_data;

    OHM_DEBUG(DBG_HEADSET, "Possible hal headset event received!");

    /* see what we had plugged in before this event */

    list = ohm_fact_store_get_facts_by_name(fs, fact_name);

    for (i = list; i != NULL; i = g_slist_next(i)) {
        OhmFact *of = i->data;
        GValue *gval = ohm_fact_get(of, "name");

        if (G_VALUE_TYPE(gval) == G_TYPE_STRING) {
            const gchar *value = g_value_get_string(gval);

            OHM_DEBUG(DBG_HEADSET, "searching audio_device_accessible: '%s'", value);

            GValue *state = NULL;
            /* OHM_DEBUG(DBG_HEADSET, "field/value: '%s'/'%s'\n", field_name, value); */
            if (strcmp(value, "headset") == 0) {
                state = ohm_fact_get(of, "connected");

                if (G_VALUE_TYPE(state) != G_TYPE_INT)
                    break;

                had_set = g_value_get_int(state) ? TRUE : FALSE;

                if (had_set) {
                    OHM_DEBUG(DBG_HEADSET, "had headset!");
                    break; /* success case */
                }
            }
            else if (strcmp(value, "headphone") == 0) {
                state = ohm_fact_get(of, "connected");

                if (G_VALUE_TYPE(state) != G_TYPE_INT)
                    break;

                had_phones = g_value_get_int(state) ? TRUE : FALSE;
                
                if (had_phones) {
                    OHM_DEBUG(DBG_HEADSET, "had headphone!");
                    break; /* success case */
                }
            }
            else if (strcmp(value, "headmike") == 0) {
                state = ohm_fact_get(of, "connected");

                if (G_VALUE_TYPE(state) != G_TYPE_INT)
                    break;

                had_mic = g_value_get_int(state) ? TRUE : FALSE;

                if (had_mic) {
                    OHM_DEBUG(DBG_HEADSET, "had headmike!");
                    break; /* success case */
                }
            }
            else if (strcmp(value, "tvout") == 0) {
                state = ohm_fact_get(of, "connected");

                if (G_VALUE_TYPE(state) != G_TYPE_INT)
                    break;

                had_video_out = g_value_get_int(state) ? TRUE : FALSE;

                if (had_video_out) {
                    OHM_DEBUG(DBG_HEADSET, "had video-out!");
                    break; /* success case */
                }
            }
            else if (strcmp(value, "line-out") == 0) {
                state = ohm_fact_get(of, "connected");

                if (G_VALUE_TYPE(state) != G_TYPE_INT)
                    break;

                had_line_out = g_value_get_int(state) ? TRUE : FALSE;

                if (had_line_out) {
                    OHM_DEBUG(DBG_HEADSET, "had line-out!");
                    break; /* success case */
                }
            }
        }
    }

    capabilities = ohm_fact_get(hal_fact, "input.jack.type");

    if (capabilities == NULL) {
        /* OHM_DEBUG(DBG_HEADSET, "Headset removed or something?\n"); */
    }
    else if (G_VALUE_TYPE(capabilities) == G_TYPE_STRING) {
        const gchar *escaped_caps = g_value_get_string(capabilities);
#define STRING_DELIMITER "\\"
        gchar **caps = g_strsplit(escaped_caps, STRING_DELIMITER, 0);
#undef STRING_DELIMITER
        gchar **caps_iter = caps;
        gboolean has_mic = FALSE, has_phones = FALSE, has_set = FALSE, has_line_out = FALSE, has_video_out = FALSE;

        for (; *caps_iter != NULL; caps_iter++) {
            gchar *cap = *caps_iter;

            if (cap) {
                if (strcmp(cap, "headphone") == 0) {
                    has_phones = TRUE;
                }
                if (strcmp(cap, "microphone") == 0) {
                    has_mic = TRUE;
                }
                if (strcmp(cap, "line-out") == 0) {
                    has_line_out = TRUE;
                }
                if (strcmp(cap, "video-out") == 0) {
                    has_video_out = TRUE;
                }
            }
        }
        
        g_strfreev(caps);


        /* TODO: As the number of the things that are inserted to the
         * headset plug is increasing (and the code is getting more
         * complicated), it might make sense to actually implement a
         * state machine to keep track of the plug states. */

        /* See if the accessory state has changed. Note bit complicated
         * handling of headset issues. :-) */

        video_changed = (has_video_out != had_video_out);
        line_changed = (has_line_out != had_line_out);
        
        /* If we had a set and still have a set, nothing has changed.
         * Otherwise check the phones and mic. */
        has_set = has_phones && has_mic;
        headset_changed = (!(has_set && had_set) &&
               ((has_phones != had_phones) ||
                (has_mic != had_mic) ||
                (has_set != had_set)));

        OHM_DEBUG(DBG_HEADSET, "starting to change headset stuff...\n");
        OHM_DEBUG(DBG_HEADSET, "Previous state: had_set=%i, had_mic=%i, had_phones=%i, had_video_out=%i, had_line_out=%i", had_set, had_mic, had_phones, had_video_out, had_line_out);
        OHM_DEBUG(DBG_HEADSET, "Current state: has_set=%i, has_mic=%i, has_phones=%i, has_video_out=%i, has_line_out=%i", has_set, has_mic, has_phones, has_video_out, has_line_out);

        OHM_DEBUG(DBG_HEADSET, "headset_changed=%i, video_changed=%i, line_changed=%i", headset_changed, video_changed, line_changed);
        
        if (video_changed || line_changed || headset_changed) {

            found = TRUE; /* something did change */
            
            /* we remove what we had */
            
            if (had_set && !has_set) {
                OHM_DEBUG(DBG_HEADSET, "removed headset!");
                dres_accessory_request("headset", -1, 0);
            }
            else if (had_mic) {
                OHM_DEBUG(DBG_HEADSET, "removed headmike!");
                dres_accessory_request("headmike", -1, 0);
            }
            else if (had_phones) {
                OHM_DEBUG(DBG_HEADSET, "removed headphones!");
                dres_accessory_request("headphone", -1, 0);
            }

            if (had_line_out && !has_line_out) {
                OHM_DEBUG(DBG_HEADSET, "removed line-out!");
                /* not supported ATM */
                /* dres_accessory_request("line_out", -1, 0); */
            }
            if (had_video_out && !has_video_out) {
                OHM_DEBUG(DBG_HEADSET, "removed video-out!");
                dres_accessory_request("tvout", -1, 0);
            }

            /* we add the current stuff */

            if (has_set) {
                OHM_DEBUG(DBG_HEADSET, "inserted headset!");
                dres_accessory_request("headset", -1, 1);
            }
            else if (has_mic) {
                OHM_DEBUG(DBG_HEADSET, "inserted headmike!");
                dres_accessory_request("headmike", -1, 1);
            }
            else if (has_phones) {
                OHM_DEBUG(DBG_HEADSET, "inserted headphones!");
                dres_accessory_request("headphone", -1, 1);
            }
            
            if (has_line_out && !had_line_out) {
                OHM_DEBUG(DBG_HEADSET, "inserted line-out!");
                /* not supported ATM */
                /* dres_accessory_request("line_out", -1, 1); */
            }
            if (has_video_out && !had_video_out) {
                OHM_DEBUG(DBG_HEADSET, "inserted video-out!");
                dres_accessory_request("tvout", -1, 1);
            }

            dres_all();

        }
        else {
            OHM_DEBUG(DBG_HEADSET, "Nothing changed");
        }
        OHM_DEBUG(DBG_HEADSET, "...done.");
    }

    return found;
}

gboolean headset_deinit(OhmPlugin *plugin)
{
    (void) plugin;

    return local_unset_observer(token);
}

gboolean headset_init(OhmPlugin *plugin, int flag_headset)
{
    (void) plugin;

    DBG_HEADSET = flag_headset;

    return local_set_observer("input.jack", complete_headset_cb, token);
}

