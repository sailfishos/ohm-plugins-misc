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


#ifndef __OHM_MDM_H__
#define __OHM_MDM_H__

enum mdm_result {
    MDM_RESULT_SUCCESS,
    MDM_RESULT_UNKNOWN,
    MDM_RESULT_ERROR
};

struct mdm_entry {
    char *name;
    char *value;
    char *requested_value;
};

void mdm_init(OhmPlugin *plugin);
void mdm_exit(OhmPlugin *plugin);
int mdm_request(const char *name, const char *value);

const struct mdm_entry *mdm_entry_get(const char *name);
const GSList *mdm_entry_get_all();

#endif
