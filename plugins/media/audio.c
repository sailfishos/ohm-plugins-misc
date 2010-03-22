/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>


#include "plugin.h"
#include "audio.h"
#include "dbusif.h"
#include "fsif.h"

static void  audio_stream_changed_cb(fsif_entry_t *, char *,
				     fsif_fact_watch_e,void *);

/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void audio_init(OhmPlugin *plugin)
{
    (void)plugin;

    fsif_add_fact_watch(FACTSTORE_AUDIO_STREAM, fact_watch_insert,
			audio_stream_changed_cb, NULL);
    fsif_add_fact_watch(FACTSTORE_AUDIO_STREAM, fact_watch_remove,
			audio_stream_changed_cb, NULL);
}



/*!
 * @}
 */

static void  audio_stream_changed_cb(fsif_entry_t      *entry,
				     char              *name,
				     fsif_fact_watch_e  event,
				     void              *usrdata)
{
    char     *oper    = "<unknown>";
    uint32_t  pid     = 0;
    char     *group   = NULL;
    char     *propnam = "media.name";
    char     *method  = "<unknown>";
    char     *pattern = "<unknown>";

    (void)usrdata;

    switch (event) {
    case fact_watch_insert:     oper = "register";                      break;
    case fact_watch_remove:     oper = "unregister";                    break;
    default: OHM_ERROR("media: invalid factstore event %d", event);     return;
    }

    fsif_get_field_by_entry(entry, fldtype_integer, "pid"     , &pid    );
    fsif_get_field_by_entry(entry, fldtype_string , "group"   , &group  );
    fsif_get_field_by_entry(entry, fldtype_string , "property", &propnam);
    fsif_get_field_by_entry(entry, fldtype_string , "method"  , &method );
    fsif_get_field_by_entry(entry, fldtype_string , "pattern" , &pattern);

    OHM_DEBUG(DBG_AUDIO, "audio stream %s: pid=%u group='%s' property='%s' "
              "method=%s pattern='%s'", oper, pid, group?group:"<null>",
              propnam, method, pattern);

    if (pid != 0  &&  group != NULL   &&
        !strcmp(propnam,"media.name") &&
        !strcmp(method,"equals")         )
    {
        /*
         * due to the current pulseaudio-policy-enforcment
         * for the time being we support just a subset limited to
         * stream name (ie.media.name property) equals to the
         * provided pattern
         */
        dbusif_send_audio_stream_info(oper, group, pid, pattern);
    }
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
