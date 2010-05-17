/*! \defgroup pubif Public Interfaces */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "plugin.h"
#include "mem.h"


#ifdef malloc
#undef malloc
#undef calloc
#undef strdup
#undef free
#endif

static FILE *mfile;


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void mem_init(OhmPlugin *plugin)
{
    const char *path;

    path  = ohm_plugin_get_param(plugin, "memtrace");
    mfile = path ? fopen(path, "w+") : NULL;
}

void mem_exit(OhmPlugin *plugin)
{
    (void)plugin;

    if (mfile != NULL)
        fclose(mfile);
}

void *__malloc(const char *file, int line, size_t size)
{
    void *mem;


    if ((mem = malloc(size)) != NULL) {
        if (mfile != NULL) {
            fprintf(mfile, "0x%08x 0x%08x malloc %s %d\n",
                    (unsigned int)mem, (unsigned int)(mem + size),
                    file, line);
            fflush(mfile);
        }
    }

    return mem;
}

void *__calloc(const char *file, int line, size_t nmemb, size_t size)
{
    void *mem;

    if ((mem = calloc(nmemb, size)) != NULL) {
        if (mfile != NULL) {
            fprintf(mfile, "0x%08x 0x%08x calloc %s %d\n",
                    (unsigned int)mem, (unsigned int)(mem + (nmemb*size)),
                    file, line);
            fflush(mfile);
        }
    }

    return mem;
}

char *__strdup(const char *file, int line, const char *string)
{
    char *dup;

    if ((dup = strdup(string)) != NULL) {
        if (mfile != NULL) {
            fprintf(mfile, "0x%08x 0x%08x strdup %s %d\n",
                    (unsigned int)dup, (unsigned int)(dup+(strlen(string)+1)),
                    file, line);
            fflush(mfile);
        }
    }
    
    return dup;
}

void __free(const char *file, int line, void *mem)
{
    if (mfile != NULL) {
        fprintf(mfile, "0x%08x            free   %s %d\n",
                (unsigned int)mem, file, line);
        fflush(mfile);
    }

    free(mem);
}

/*!
 * @}
 */


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
