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


#ifndef __OHM_RESOURCE_FSIF_H__
#define __OHM_RESOURCE_FSIF_H__

/*
 * FIXME:
 * The 'delay' and the 'timer' plugin share the similar fsif.[hc]
 * However, do to the rush, they are currently duplicated (ie. the same
 * code is duplicated in both directory.
 * Before we fix this pls. pay attention to kepp them identical (ie. make
 * identical changes to both copy)
 *
 */


#include <sys/time.h>

typedef enum {
    fact_watch_unknown = 0,
    fact_watch_insert,
    fact_watch_remove
} fsif_fact_watch_e;

typedef enum {
    fldtype_invalid = 0,
    fldtype_string,
    fldtype_integer,
    fldtype_unsignd,
    fldtype_floating,
    fldtype_time,
} fsif_fldtype_t;

typedef union {
    /* input field types */
    char               *string;
    long                integer;
    unsigned long       unsignd;
    double              floating;
    unsigned long long  time;

    /* output field types */
    void               *retval;
} fsif_value_t;

typedef struct {
    fsif_fldtype_t  type;
    char           *name;
    fsif_value_t    value;
} fsif_field_t;

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;
typedef struct _OhmFact   fsif_entry_t;

typedef void (*fsif_field_watch_cb_t)(fsif_entry_t *, char *, fsif_field_t *,
                                      void *);
typedef void (*fsif_fact_watch_cb_t)(fsif_entry_t *, char *, fsif_fact_watch_e,
                                     void *);

void fsif_init(OhmPlugin *);
void fsif_exit(OhmPlugin *);
int  fsif_add_factstore_entry(char *, fsif_field_t *);
int  fsif_delete_factstore_entry(char *, fsif_field_t *);
int  fsif_update_factstore_entry(char *, fsif_field_t *,fsif_field_t *);
void fsif_get_field_by_entry(fsif_entry_t *, fsif_fldtype_t, char *, void *);
int  fsif_get_field_by_name(const char *, fsif_fldtype_t, char *, void *);
int  fsif_add_fact_watch(char *,fsif_fact_watch_e,fsif_fact_watch_cb_t,void *);
int  fsif_add_field_watch(char *, fsif_field_t *, char *,
                          fsif_field_watch_cb_t, void *);


#endif /* __OHM_RESOURCE_FSIF_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
