
static int dresif_power_key_pressed(void)
{
#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)

    static char *target = "power_key_pressed";

    char *vars[10];
    int   i;
    int   status;


    OHM_DEBUG(DBG_DRES, "Resolving target '%s'", target);

    vars[i=0] = NULL;

    status = resolve(target, vars);

    return status;

#undef DRESIF_VARVALUE
#undef DRESIF_VARTYPE
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
