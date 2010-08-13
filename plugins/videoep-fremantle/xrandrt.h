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


#ifndef __OHM_XRANDRT_H__
#define __OHM_XRANDRT_H__

#include <X11/Xlib.h>
#include <glib.h>

typedef struct xrandrt_s {
    Display      *disp;
    GIOChannel   *chan;
    guint         evsrc;
} xrandrt_t;

static xrandrt_t *xrandrt_init(const char *);
static void xrandrt_exit(xrandrt_t *);


#endif /* __OHM_XRANDRT_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
