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


#ifndef __OHM_VIDEOEP_MEM_H__
#define __OHM_VIDEOEP_MEM_H__

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

#ifdef BUILTIN_MEMORY_TRACE
#define malloc(s)   mem_malloc(__FILE__, __LINE__, s)
#define calloc(n,s) mem_calloc(__FILE__, __LINE__, n, s)
#define strdup(s)   mem_strdup(__FILE__, __LINE__, s)
#define free(m)     mem_free(__FILE__, __LINE__, m)
#endif

void mem_init(OhmPlugin *);
void mem_exit(OhmPlugin *);

void *mem_malloc(const char *, int, size_t);
void *mem_calloc(const char *, int, size_t, size_t);
char *mem_strdup(const char *, int, const char *);
void  mem_free(const char *, int, void *);


#endif /* __OHM_VIDEOEP_MEM_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
