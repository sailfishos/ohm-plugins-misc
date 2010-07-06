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


#include <glib.h>

#include "dbus-plugin.h"


/********************
 * hash_table_create
 ********************/
hash_table_t *
hash_table_create(void (*key_free)(void *), void (*value_free)(void *))
{
    return g_hash_table_new_full(g_str_hash, g_str_equal, key_free, value_free);
}


/********************
 * hash_table_destroy
 ********************/
void
hash_table_destroy(hash_table_t *ht)
{
    g_hash_table_destroy(ht);
}


/********************
 * hash_table_insert
 ********************/
int
hash_table_insert(hash_table_t *ht, char *key, void *value)
{
    g_hash_table_insert(ht, key, value);
    return TRUE;
}


/********************
 * hash_table_lookup
 ********************/
void *
hash_table_lookup(hash_table_t *ht, const char *key)
{
    return g_hash_table_lookup(ht, key);
}


/********************
 * hash_table_remove
 ********************/
int
hash_table_remove(hash_table_t *ht, const char *key)
{
    return g_hash_table_remove(ht, key);
}


/********************
 * hash_table_unhash
 ********************/
int
hash_table_unhash(hash_table_t *ht, const char *key)
{
    return g_hash_table_steal(ht, key);
}


/********************
 * hash_table_empty
 ********************/
int
hash_table_empty(hash_table_t *ht)
{
    return g_hash_table_size(ht) == 0;
}


/********************
 * hash_table_foreach
 ********************/
void
hash_table_foreach(hash_table_t *ht, GHFunc callback, void *data)
{
    g_hash_table_foreach(ht, callback, data);
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

