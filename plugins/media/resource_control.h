/*************************************************************************
Copyright (C) 2010 Nokia Corporation.
              2013 Jolla Ltd.

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

#ifndef __OHM_MEDIA_RESOURCE_CONTROL_H__
#define __OHM_MEDIA_RESOURCE_CONTROL_H__

void resctl_init(void);
void resctl_exit(void);
void resctl_acquire(void);
void resctl_release(void);

#endif /*  __OHM_MEDIA_RESOURCE_CONTROL_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
