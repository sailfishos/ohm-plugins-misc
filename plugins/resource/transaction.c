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


/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "plugin.h"
#include "transaction.h"

#define HASH_BITS      10
#define HASH_DIM       (1 << HASH_BITS)
#define HASH_MASK      (HASH_DIM - 1)
#define HASH_INDEX(i)  ((i) & HASH_MASK)

#define ALLOC_DIM      1024
#define ALLOC_JUNK     (ALLOC_DIM * sizeof(uint32_t))


typedef struct {
    int       junks;
    int       length;
    uint32_t *table;    
} resset_table_t;

typedef struct {
    transaction_callback_t  function;
    void                   *user_data;
} completion_t;

typedef struct {
    uint32_t           id;
    int                refcnt;
    resset_table_t     resset;
    completion_t       completion;
} transaction_t;


static transaction_t   transactions[HASH_DIM];
static uint32_t        txwrite;
static uint32_t        txread = 1;

static transaction_t *find_transaction(uint32_t);
static int add_resource_set(transaction_t *, uint32_t);
static void complete_transaction(uint32_t);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void transaction_init(OhmPlugin *plugin)
{
    (void)plugin;

    ENTER;

    LEAVE;
}

uint32_t transaction_create(transaction_callback_t callback, void *user_data)
{
    static uint32_t  count = NO_TRANSACTION;

    uint32_t       txid = ++count;
    int            idx  = HASH_INDEX(txid);
    transaction_t *tx   = transactions + idx;

    if (tx->id != NO_TRANSACTION) {
        OHM_ERROR("resource: transaction table overflow: transaction %u",txid);
        txid = NO_TRANSACTION;
    }
    else {
        memset(tx, 0, sizeof(transaction_t));
        tx->id     = txid;
        tx->refcnt = 1;

        tx->completion.function  = callback;
        tx->completion.user_data = user_data;

        txwrite = txid;

        OHM_DEBUG(DBG_TRANSACT, "transaction %u created", txid);
    }

    return txid;
}

int transaction_add_resource_set(uint32_t txid, uint32_t rsid)
{
    transaction_t *tx = find_transaction(txid);
    int success;

    if (tx == NULL)
        success = FALSE;
    else
        success = add_resource_set(tx, rsid);

    if (success) {
        OHM_DEBUG(DBG_TRANSACT, "resource set (manager_id %u) "
                  "added to transaction %u", rsid, txid);
    }

    return success;
}

int transaction_ref(uint32_t txid)
{
    transaction_t *tx = find_transaction(txid);
    int success;

    if (tx == NULL)
        success = FALSE;
    else {
        success = TRUE;
        tx->refcnt++;

        OHM_DEBUG(DBG_TRANSACT, "transaction referenced (refcnt is %u)",
                  tx->refcnt);
    }

    return success;
}

int transaction_unref(uint32_t txid)
{
    transaction_t *tx = find_transaction(txid);
    int success;

    if (tx == NULL || tx->refcnt <= 0)
        success = FALSE;
    else {
        success = TRUE;
        tx->refcnt--;

        OHM_DEBUG(DBG_TRANSACT, "transaction unreferenced (refcnt is %u)",
                  tx->refcnt);

        if (tx->refcnt <= 0)
            complete_transaction(txid);
    }

    return success;
}


/*!
 * @}
 */

static transaction_t *find_transaction(uint32_t txid)
{
    int            idx = HASH_INDEX(txid);
    transaction_t *tx  = transactions + idx;

    return (txid != NO_TRANSACTION && txid == tx->id) ? tx : NULL;
}


static int add_resource_set(transaction_t *tx, uint32_t rsid)
{
    int   idx   = tx->resset.length;
    int   junks;
    int   size;
    void *mem;
    int   i;

    for (i = 0;    i < tx->resset.length;   i++) {
        if (tx->resset.table[i] == rsid)
            return TRUE;        /* it is already there */
    }
    
    if ((junks = (idx + ALLOC_JUNK) / ALLOC_JUNK) > tx->resset.junks) {
        size = junks * ALLOC_JUNK * sizeof(uint32_t *);
        mem  = realloc(tx->resset.table, size);
        
        if (mem == NULL)
            return FALSE;

        tx->resset.junks = junks;
        tx->resset.table = mem;
    }

    tx->resset.length = idx + 1;
    tx->resset.table[idx] = rsid;

    return TRUE;
}

static void complete_transaction(uint32_t txid)
{
    transaction_t *tx;
    uint32_t       id;

    if (txid == txread) {
        for (id = txread;  id <= txwrite;   id++)  {
            if ((tx = find_transaction(id)) == NULL) {
                OHM_ERROR("resource: wants to complete transaction %u "
                          "but can't find it", id);
                continue;
            }

            if (tx->refcnt > 0)
                break;

            OHM_DEBUG(DBG_TRANSACT, "completing transaction %u", tx->id);

            if (tx->completion.function != NULL) {
                tx->completion.function(tx->resset.table, tx->resset.length,
                                        tx->id, tx->completion.user_data);
            }
        
            free(tx->resset.table);
            memset(tx, 0, sizeof(*tx));
            
            txread = id + 1;
        }
    }
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
