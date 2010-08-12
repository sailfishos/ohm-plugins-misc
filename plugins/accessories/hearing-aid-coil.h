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


#ifndef __HEARING_AID_COIL_H__
#define __HEARING_AID_COIL_H__

#include <ohm/ohm-plugin.h>


#define FACT_NAME_GCONF        "com.nokia.policy.gconf"
#define GCONF_PATH             "/system/hearing_aid_coil"


void hearing_aid_coil_init(OhmPlugin *plugin, int dbg_hac);
void hearing_aid_coil_exit(OhmPlugin *plugin);

#endif /* __HEARING_AID_COIL_H__ */
