/*************************************************************************
Copyright (C) 2010 Nokia Corporation.
              2016 Jolla Ltd.

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


#ifndef __OHM_ROUTE_DRESIF_H__
#define __OHM_ROUTE_DRESIF_H__

enum dresif_result {
    DRESIF_RESULT_SUCCESS,
    DRESIF_RESULT_DENIED,
    DRESIF_RESULT_ERROR
};

void dresif_init(OhmPlugin *plugin);
int dresif_set_feature(const char *feature, int enabled);
int dresif_set_prefer(const char *route, int set);

#endif
