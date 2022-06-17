/*************************************************************************
Copyright (C) 2016 Jolla Ltd.

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


#ifndef __OHM_ROUTE_ROUTE_H__
#define __OHM_ROUTE_ROUTE_H__

#include <stdint.h>

enum feature_result {
    FEATURE_RESULT_SUCCESS,
    FEATURE_RESULT_DENIED,
    FEATURE_RESULT_UNKNOWN,
    FEATURE_RESULT_ERROR
};

enum prefer_result {
    PREFER_RESULT_SUCCESS,
    PREFER_RESULT_DENIED,
    PREFER_RESULT_UNKNOWN,
    PREFER_RESULT_ERROR
};

struct audio_feature {
    char *name;
    unsigned int allowed;
    unsigned int enabled;
};

struct audio_device_mapping;

void route_init(OhmPlugin *plugin);
void route_exit(OhmPlugin *plugin);
int route_query_active(const char **sink, unsigned int *sink_type,
                       const char **source, unsigned int *source_type);
int context_variable_query(char *name, char **value);

int route_feature_request(const char *name, int enable);
int route_prefer_request(const char *name, uint32_t type, uint32_t set);

const GSList *route_get_features();
const GSList *route_get_mappings();
const char *route_mapping_name(const struct audio_device_mapping *mapping);
int route_mapping_type(const struct audio_device_mapping *mapping);

#endif
