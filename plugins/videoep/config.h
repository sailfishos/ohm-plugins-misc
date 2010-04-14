#ifndef __OHM_VIDEOEP_CONFIG_H__
#define __OHM_VIDEOEP_CONFIG_H__

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void config_init(OhmPlugin *);
void config_exit(OhmPlugin *);

int  config_parse_file(const char *);

#endif /* __OHM_VIDEOEP_CONFIG_PRIVATE_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
