#ifndef __OHM_NOTIFICATION_RULEIF_H__
#define __OHM_NOTIFICATION_RULEIF_H__

#define RULEIF_STRING_ARG(n,v)     n, (int)'s', (void *)&(v)
#define RULEIF_INTEGER_ARG(n,v)    n, (int)'i', (void *)&(v)
#define RULEIF_DOUBLE_ARG(n,v)     n, (int)'d', (void *)&(v)
#define RULEIF_ARGLIST_END         NULL, (int)0, (void *)0 


/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void ruleif_init(OhmPlugin *);
int  ruleif_notification_request(const char *, ...);
int  ruleif_notification_events(int, char **, char ***, int *);

#endif	/* __OHM_NOTIFICATION_RULEIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
