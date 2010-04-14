#ifndef __OHM_VIDEOEP_MEM_H__
#define __OHM_VIDEOEP_MEM_H__

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

#define malloc(s)   __malloc(__FILE__, __LINE__, s)
#define calloc(n,s) __calloc(__FILE__, __LINE__, n, s)
#define strdup(s)   __strdup(__FILE__, __LINE__, s)
#define free(m)     __free(__FILE__, __LINE__, m)


void mem_init(OhmPlugin *);
void mem_exit(OhmPlugin *);

void *__malloc(const char *, int, size_t);
void *__calloc(const char *, int, size_t, size_t);
char *__strdup(const char *, int, const char *);
void  __free(const char *, int, void *);


#endif /* __OHM_VIDEOEP_MEM_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
