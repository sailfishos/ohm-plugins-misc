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


#include "accessories.h"
#include "bluetooth-common.h"


#ifdef ENABLE_BT_TRACE
void assert_iter_f(const char *func, int lineno, DBusMessageIter *iter, int expected_arg_type)
{
    int arg_type;

    if ((arg_type = dbus_message_iter_get_arg_type(iter)) != expected_arg_type) {
        OHM_ERROR("accessories-bt: %s():%d Parsing DBus iterator, expected %c, found %c.",
                  func, lineno, expected_arg_type, arg_type);
        abort();
    }
}
#endif
