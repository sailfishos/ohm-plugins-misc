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
int  ruleif_notification_events(int, char ***, int *);
int  ruleif_notification_play_short(int, int *);

#endif	/* __OHM_NOTIFICATION_RULEIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
