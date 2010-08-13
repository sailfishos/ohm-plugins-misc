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


static void request_init(OhmPlugin *plugin)
{
    (void)plugin;
}

OHM_EXPORTABLE(int, delay_execution, (unsigned long delay, char *id,
                                      int restart, char *cb_name,delay_cb_t cb,
                                      char *argt, void **argv))
{
    fsif_entry_t *entry;
    int           success;

    OHM_DEBUG(DBG_REQUEST, "%s(delay=%u id='%s', restart=%d, cb='%s', "
              "argt='%s', argv=%p)", __FUNCTION__, delay, id, restart,
              cb_name, argt, argv);

    entry = timer_lookup(id);

    if (restart) {
        if (entry == NULL)
            success = timer_add(id, delay, cb_name,cb, argt,argv);
        else
            success = timer_restart(entry, delay, cb_name,cb, argt,argv);
    }
    else {
        if (entry == NULL)
            success = timer_add(id, delay, cb_name,cb, argt,argv);
        else
            success = FALSE;
    }

    return success;
}

OHM_EXPORTABLE(int, delay_cancel, (char *id))
{
    fsif_entry_t *entry;
    int           success;

    OHM_DEBUG(DBG_REQUEST, "%s(id='%s')", __FUNCTION__, id);

    if ((entry = timer_lookup(id)) == NULL)
        success = FALSE;
    else
        success = timer_stop(entry);


    return success;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
