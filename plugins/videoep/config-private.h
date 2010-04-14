#ifndef __OHM_VIDEOEP_CONFIG_PRIVATE_H__
#define __OHM_VIDEOEP_CONFIG_PRIVATE_H__

#include "config.h"
#include "data-types.h"
#include "tracker.h"
#include "sequence.h"

typedef enum {
    windef_unknow = 0,
    windef_property,
} config_windef_type_t;


typedef struct {
    config_windef_type_t  type;
    union {
        struct {
            char       *name;
            exec_def_t *exec;
        }                 property;
    };
} config_windef_t; 

typedef struct {
    int  first;
    int  last;
} yy_column;

extern yy_column yy_videoep_column;

int scanner_open_file(const char *);

int  yy_videoep_lex(void);
int  yy_videoep_parse(void);
void yy_videoep_error(const char *);


#endif /* __OHM_VIDEOEP_CONFIG_PRIVATE_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
