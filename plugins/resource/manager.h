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


#ifndef __OHM_RESOURCE_MANAGER_H__
#define __OHM_RESOURCE_MANAGER_H__

#include <res-conn.h>

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void manager_init(OhmPlugin *);

void manager_register(resmsg_t *, resset_t *, void *);
void manager_unregister(resmsg_t *, resset_t *, void *);
void manager_update(resmsg_t *, resset_t *, void *);
void manager_acquire(resmsg_t *, resset_t *, void *);
void manager_release(resmsg_t *, resset_t *, void *);
void manager_audio(resmsg_t *, resset_t *, void *);
void manager_video(resmsg_t *, resset_t *, void *);

#endif	/* __OHM_RESOURCE_MANAGER_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
