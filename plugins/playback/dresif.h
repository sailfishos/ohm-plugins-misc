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


#ifndef __OHM_PLAYBACK_DRESIF_H__
#define __OHM_PLAYBACK_DRESIF_H__

static void dresif_init(OhmPlugin *);
static int  dresif_group_request(char *, int);
static int  dresif_playback_state_request(client_t *, char *, int);
static int  dresif_privacy_override_request(int, int);
static int  dresif_bluetooth_override_request(int, int);
static int  dresif_mute_request(int, int);


#endif /* __OHM_PLAYBACK_DRESIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

