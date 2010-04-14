/*! \defgroup pubif Public Interfaces */

#include <stdlib.h>

#include "data-types.h"

static videoep_arg_t *actual_argument(videoep_arg_t *arg);
static int            actual_index(videoep_arg_t *, int);

/*! \addtogroup pubif
 *  Functions
 *  @{
 */

videoep_value_type_t videoep_get_argument_type(videoep_arg_t *arg)
{
    videoep_arg_t *aarg = actual_argument(arg);

    return aarg->type;
}

uint32_t  videoep_get_argument_dimension(videoep_arg_t *arg)
{
    videoep_arg_t *aarg = actual_argument(arg);

    return aarg->dim;
}

void *videoep_get_argument_data(videoep_arg_t *arg)
{
    videoep_arg_t *aarg = actual_argument(arg);

    return aarg->value.pointer;
}

void *videoep_get_pointer_argument(videoep_arg_t *arg)
{
    videoep_arg_t *aarg = actual_argument(arg);

    return (aarg->type == videoep_pointer) ? aarg->value.pointer : NULL;
}

char *videoep_get_string_argument(videoep_arg_t *arg)
{
    videoep_arg_t *aarg = actual_argument(arg);

    return (aarg->type == videoep_string) ? aarg->value.string : NULL;
}

uint32_t videoep_get_unsigned_argument(videoep_arg_t *arg, int idx)
{
    videoep_arg_t *aarg = actual_argument(arg);
    int            aidx = actual_index(aarg, idx);

    return (aarg->type == videoep_unsignd) ? aarg->value.unsignd[aidx] : 0;
}

int32_t videoep_get_integer_argument(videoep_arg_t *arg, int idx)
{
    videoep_arg_t *aarg = actual_argument(arg);
    int            aidx = actual_index(aarg, idx);

    return (aarg->type == videoep_integer) ? aarg->value.integer[aidx] : 0;
}


/*!
 * @}
 */

static videoep_arg_t *actual_argument(videoep_arg_t *arg)
{
    static videoep_arg_t noarg;

    videoep_arg_t *aarg = arg;

    if (arg && arg->type == videoep_link)
        aarg = arg->value.link;

    return aarg ? aarg : &noarg;
}

static int actual_index(videoep_arg_t *arg, int idx)
{
    int aidx = 0;
    int dim  = (int)arg->dim;

    if (idx >= 0 && idx < dim)
        aidx = idx;
    else {
        if (idx < 0) {
            aidx = idx + dim;

            if (aidx < 0   )  aidx = 0; else
            if (aidx >= dim)  aidx = dim - 1;
        }
    }

    return aidx;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
