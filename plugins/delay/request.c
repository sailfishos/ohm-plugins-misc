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
