/*************************************************************************
 * Copyright (C) 2010 Intel Corporation.
 *
 * These OHM Modules are free software; you can redistribute
 * it and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA.
 * *************************************************************************/
#ifndef __OHM_FACTTOOL_H__
#define __OHM_FACTTOOL_H__

#include <sys/types.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <gmodule.h>
#include <glib.h>
#include <glib-object.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-plugin-debug.h>

#define DBUS_INTERFACE_POLICY    "com.nokia.policy"
#define DBUS_PATH_POLICY         "/com/nokia/policy"

#define METHOD_POLICY_FACTTOOL_SET_FACT			"setfact"
#define METHOD_POLICY_FACTTOOL_GET_FACT			"getfact"

static void plugin_init(OhmPlugin *);
static void plugin_exit(OhmPlugin *);

#endif /* __OHM_FACTTOOL_H__ */
