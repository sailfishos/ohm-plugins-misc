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


#ifndef __OHM_VIDEOEP_DATA_TYPES_H__
#define __OHM_VIDEOEP_DATA_TYPES_H__

#include <stdint.h>

typedef enum videoep_value_type_e {
    /* this stuff should match their
       predefined Atom value */
    videoep_unknown  =  0,
    videoep_atom     =  4,      /* XA_ATOM */
    videoep_card     =  6,      /* XA_CARDINAL */
    videoep_string   = 31,      /* XA_STRING */
    videoep_window   = 33,      /* XA_WINDOW */

    /* non-predefined Atoms */
    videoep_wmstate  = 69,      /* XA_LAST_PREDEFINED + 1 */
    
    /* extra stuff */
    videoep_pointer,
    videoep_unsignd,
    videoep_integer,

    /* misc */
    videoep_link,               /* link to another value */
    videoep_sequence,           /* link to sequence instance */

    /* dimension for arrays */
    videoep_value_dim
} videoep_value_type_t;

typedef union {
    char     *string;
    uint32_t *atom;
    uint32_t *window;
    int32_t  *card;
    void     *generic;
} videoep_value_t;

typedef struct videoep_arg_s {
    videoep_value_type_t  type;
    union {
        void     *pointer;
        char     *string;       /* we do not support string arrays :( */
        uint32_t *unsignd;
        int32_t  *integer;
        struct videoep_arg_s *link;
        struct sequence_inst_s *seqinst;
    }                     value;
    uint32_t              dim;
} videoep_arg_t;


videoep_value_type_t videoep_get_argument_type(videoep_arg_t *);
uint32_t  videoep_get_argument_dimension(videoep_arg_t *);
void     *videoep_get_argument_data(videoep_arg_t *);

void     *videoep_get_pointer_argument(videoep_arg_t *);
char     *videoep_get_string_argument(videoep_arg_t *);
uint32_t  videoep_get_unsigned_argument(videoep_arg_t *, int);
int32_t   videoep_get_integer_argument(videoep_arg_t *, int);



#endif /* __OHM_VIDEOEP_DATA_TYPES_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

