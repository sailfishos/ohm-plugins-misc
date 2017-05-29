/*************************************************************************
Copyright (C) 2017 Jolla Ltd.

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


#ifndef __BLUETOOTH_COMMON_H__
#define __BLUETOOTH_COMMON_H__

#include "bluetooth.h"

#define BLUEZ_UUID_A2DP_SOURCE  "0000110a-0000-1000-8000-00805f9b34fb"
#define BLUEZ_UUID_A2DP_SINK    "0000110b-0000-1000-8000-00805f9b34fb"
#define BLUEZ_UUID_HSP_HS       "00001108-0000-1000-8000-00805f9b34fb"
#define BLUEZ_UUID_HSP_AG       "00001112-0000-1000-8000-00805f9b34fb"
#define BLUEZ_UUID_HFP_HF       "0000111e-0000-1000-8000-00805f9b34fb"
#define BLUEZ_UUID_HFP_AG       "0000111f-0000-1000-8000-00805f9b34fb"

#define BT_TYPE_A2DP            "bta2dp"
#define BT_TYPE_HSP             "bthsp"
#define BT_TYPE_HFP             "bthfp"
#define BT_TYPE_AUDIO           "audio"


#ifdef ENABLE_BT_TRACE

#define BT_TRACE(...) OHM_DEBUG(DBG_BT, "accessories-bt: " __VA_ARGS__)

void assert_iter_f(const char *func, int lineno, DBusMessageIter *iter, int expected_arg_type);
#define BT_ASSERT_ITER(iter, arg_type) assert_iter_f(__FUNCTION__, __LINE__, iter, arg_type)

#define BT_ASSERT(test)     \
    do {                    \
        if (!(test)) {      \
            OHM_ERROR("accessories-bt: %s():%d Assertion '%s' failed. Abort.", __FUNCTION__, __LINE__, #test); \
            abort();        \
        }                   \
    } while (0)

#else

#define BT_ASSERT_ITER(iter, arg_type) do {} while(0)
#define BT_ASSERT(test) do {} while(0)
#define BT_TRACE(...) do {} while(0)

#endif


#define BT_DEBUG(...)   OHM_DEBUG(DBG_BT, "accessories-bt: " __VA_ARGS__)
#define BT_INFO(...)    OHM_INFO("accessories-bt: " __VA_ARGS__)
#define BT_ERROR(...)   OHM_ERROR("accessories-bt: " __VA_ARGS__)

#endif
