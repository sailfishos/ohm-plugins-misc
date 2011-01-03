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


#ifndef __GCONF_TRIGGERS_H__
#define __GCONF_TRIGGERS_H__

#include <ohm/ohm-plugin.h>


#define FACT_NAME_GCONF         "com.nokia.policy.gconf"
#define GCONF_HAC_PATH          "/system/telecoil/enable"
#define GCONF_UNC_PATH          "/system/unc/enable"


void gconf_triggers_init(OhmPlugin *plugin, int dbg_mode);
void gconf_triggers_exit(OhmPlugin *plugin);

#endif /* __GCONF_TRIGGERS_H__ */
