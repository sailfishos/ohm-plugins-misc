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

