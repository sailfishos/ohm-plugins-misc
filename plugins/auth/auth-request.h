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


#ifndef __OHM_AUTH_REQUEST_H__
#define __OHM_AUTH_REQUEST_H__

typedef int  (*auth_request_cb_t)(int, const char *, void *);

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void auth_request_init(OhmPlugin *);
void auth_request_exit(OhmPlugin *);

int auth_request(char *,void *, char *,void *, auth_request_cb_t,void *);


#endif /* __OHM_AUTH_REQUEST_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
