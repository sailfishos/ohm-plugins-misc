#ifndef __OHM_PLUGIN_CGRP_MM_H__
#define __OHM_PLUGIN_CGRP_MM_H__

#include <stdlib.h>
#include <string.h>

#ifndef ALLOC

#define ALLOC(type) ({                            \
            type   *__ptr;                        \
            size_t  __size = sizeof(type);        \
                                                  \
            if ((__ptr = malloc(__size)) != NULL) \
                memset(__ptr, 0, __size);         \
            __ptr; })

#define ALLOC_OBJ(ptr) ((ptr) = ALLOC(typeof(*ptr)))

#define ALLOC_ARR(type, n) ({                     \
            type   *__ptr;                        \
            size_t   __size = (n) * sizeof(type); \
                                                  \
            if ((__ptr = malloc(__size)) != NULL) \
                memset(__ptr, 0, __size);         \
            __ptr; })

#define REALLOC_ARR(ptr, o, n) ({                                       \
            typeof(ptr) __ptr;                                          \
            size_t      __size = sizeof(*ptr) * (n);                    \
                                                                        \
            if ((ptr) == NULL) {                                        \
                (__ptr) = ALLOC_ARR(typeof(*ptr), n);                   \
                ptr = __ptr;                                            \
            }                                                           \
            else if ((__ptr = realloc(ptr, __size)) != NULL) {          \
	      if ((unsigned)(n) > (unsigned)(o))			\
                    memset(__ptr + (o), 0, ((n)-(o)) * sizeof(*ptr));   \
                ptr = __ptr;                                            \
            }                                                           \
            __ptr; })

#define ALLOC_VAROBJ(ptr, n, f) ({					\
      size_t __diff = ((ptrdiff_t)(&(ptr)->f[n])) - ((ptrdiff_t)(ptr)); \
      ptr = (typeof(ptr))ALLOC_ARR(char, __diff);			\
      ptr; })
                
#define FREE(obj) do { if (obj) free(obj); } while (0)

#define STRDUP(s) ({						\
      char *__s;						\
      if (__builtin_types_compatible_p(typeof(s), char []))	\
	__s = strdup(s);					\
      else							\
	__s = ((s) ? strdup(s) : strdup(""));			\
      __s; })

#endif

#endif /* __OHM_PLUGIN_CGRP_MM_H__ */
