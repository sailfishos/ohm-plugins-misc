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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <policy/videoipc.h>

#include "plugin.h"
#include "videoipc.h"
#include "atom.h"
#include "xif.h"

typedef struct {
    uint32_t  mask;
    uint64_t  time;
} update_t;


static int         fd;          /* file descriptor for shared memory */
static int         length;      /* length of the shared memory */
static videoipc_t *ipc;         /* shared memory data */
static uint32_t    mtatomidx;   /* index of the message type atom */
static update_t    update;


static int  init_shmem(void);
static void exit_shmem(void);
static int  init_shfile(int, size_t);

static int  init_message(void);
static void exit_message(void);
static int  send_message(uint32_t);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void videoipc_init(OhmPlugin *plugin)
{
    (void)plugin;

    init_shmem();
    init_message();
}

void videoipc_exit(OhmPlugin *plugin)
{
    (void)plugin;

    exit_shmem();
    exit_message();
}

void videoipc_update_start(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    update.mask = 0;
    update.time = ((uint64_t)tv.tv_sec * 1000000ULL) + (uint64_t)tv.tv_usec;
}

void videoipc_update_end(void)
{
    send_message(update.mask);
}

void videoipc_update_section(uint32_t mask, pid_t *pids, int npid)
{
#define SECTION_DATA(sec)                                              \
    secnam = #sec "users";                                             \
    maxpid = VIDEOIPC_MAX_##sec##_USERS;                               \
    idxptr = &ipc->sec##users.idx;                                     \
    oldidx = *idxptr;                                                  \
    newidx = (oldidx+1 >= DIM(ipc->sec##users.set)) ? 0 : oldidx+1;    \
    oldset = (videoipc_set_t *)&ipc->sec##users.set[oldidx];           \
    newset = (videoipc_set_t *)&ipc->sec##users.set[newidx];

    const char *secnam;
    int maxpid;
    int8_t oldidx, newidx, *idxptr;
    videoipc_set_t *oldset, *newset;

    if (pids && npid > 0) {
        switch (mask) {

        case VIDEOIPC_XVIDEO_SECTION:
            SECTION_DATA(XV);
            break;
            
        default:
            OHM_DEBUG(DBG_IPC, "unsupported section mask 0x%x", mask);
            return;
        }

        if (npid > maxpid) {
            OHM_WARNING("videoipc: too long (%d) %s PID list. "
                        "Truncating to the allowed maximum of %d",
                        secnam, npid, maxpid);
            npid = maxpid;
        }

        if (npid != oldset->npid || memcmp(pids, oldset->pids, npid)) {
            OHM_DEBUG(DBG_IPC, "%s section in videoipc shared memory updated. "
                      "New index is %u", secnam, newidx);

            newset->time = update.time;
            newset->npid = npid;
            memcpy(newset->pids, pids, npid * sizeof(pid_t));
            
            *idxptr = newidx;

            update.mask |= mask;
        }
    }

#undef SECTION_DATA
}

/*!
 * @}
 */

static int init_shmem(void)
{
    static int    flags = O_RDWR | O_CREAT;
    static mode_t mode  = S_IRUSR|S_IWUSR | S_IRGRP | S_IROTH; /* 644 */

    size_t        page;
    struct stat   st;
    
    do { /* not a loop */
        page   = sysconf(_SC_PAGESIZE);
        length = ((sizeof(videoipc_t) + page - 1) / page) * page;
        
        if ((fd = shm_open(VIDEOIPC_SHARED_OBJECT, flags, mode)) < 0) {
            OHM_ERROR("videoep: can't create shared memory object '%s': %s",
                      VIDEOIPC_SHARED_OBJECT, strerror(errno));
            break;
        }
        
        if (fstat(fd, &st) < 0) {
            OHM_ERROR("videoep: failed to stat shared memory object '%s': %s",
                      VIDEOIPC_SHARED_OBJECT, strerror(errno));
            break;
        }
        
        if (st.st_size < length) {
            if (!init_shfile(fd, length))
                break;
        }
        
        if ((ipc = mmap(NULL,length, PROT_WRITE,MAP_SHARED, fd, 0)) == NOIPC) {
            OHM_ERROR("videoep: failed to map shared memory of '%s': %s",
                      VIDEOIPC_SHARED_OBJECT, strerror(errno));
            break;
        }
        
        if (ipc->version.major != VIDEOIPC_MAJOR_VERSION) {
            OHM_ERROR("videoep: shared memory '%s' version mismatch. Shared "
                      "memory version %d.%d  plugin version %d.%d",
                      VIDEOIPC_SHARED_OBJECT,
                      (int)ipc->version.major, (int)ipc->version.minor,
                      VIDEOIPC_MAJOR_VERSION , VIDEOIPC_MINOR_VERSION);
            errno = EINVAL;
            break;
        }
        else {
            if (ipc->version.minor == VIDEOIPC_MINOR_VERSION) {
                OHM_INFO("videoep: shared memory '%s' is OK (version %d.%d)",
                         VIDEOIPC_SHARED_OBJECT,
                         (int)ipc->version.major, (int)ipc->version.minor);
            }
            else {
                OHM_WARNING("videoep: shared memory '%s' version mismatch. "
                            "shmem version %d.%d <> plugin version %d.%d",
                            VIDEOIPC_SHARED_OBJECT,
                            (int)ipc->version.major, (int)ipc->version.minor,
                            VIDEOIPC_MAJOR_VERSION , VIDEOIPC_MINOR_VERSION);
            }
        }
        
        /* everything was OK */
        return TRUE;
        
    } while (0);
    
    /*
     * something went wrong
     */
    exit_shmem();
    
    return FALSE;
}

static void exit_shmem(void)
{
    if (ipc != NOIPC && length > 0)
        munmap((void *)ipc, length);
    
    if (fd >= 0)
        close(fd);

    /* shm_unlink(VIDEOIPC_SHARED_OBJECT); */
}

static int init_shfile(int fd, size_t size)
{
    char        buf[4096];
    size_t      junk;
    int         written;
    videoipc_t *vip;

    OHM_INFO("videoep: initiate shared memory object '%s'",
             VIDEOIPC_SHARED_OBJECT);

    memset(buf, 0, sizeof(buf));
    vip = (videoipc_t *)buf;

    vip->version.major = VIDEOIPC_MAJOR_VERSION;
    vip->version.minor = VIDEOIPC_MINOR_VERSION;

    while (size > 0) {
        junk  = (size > sizeof(buf)) ? sizeof(buf) : size;
        size -= junk;

        do {
            if ((written = write(fd, buf, junk)) <= 0) {
                if (errno == EINTR)
                    continue;

                OHM_ERROR("videoep: error during shared memory "
                          "initialization: %s", strerror(errno));

                return FALSE;
            }

            junk -= written;

        } while (junk > 0);

        vip->version.major = 0;
        vip->version.minor = 0;
    }

    return TRUE;
}

static int init_message(void)
{
    mtatomidx = atom_create("videoipcmt", VIDEOIPC_CLIENT_MESSAGE);

    return (mtatomidx != ATOM_INVALID_INDEX);
}

static void exit_message(void)
{
}

static int send_message(uint32_t mask)
{
    uint32_t  msgtyp  = atom_get_value(mtatomidx);
    uint32_t  rwin;
    int       success;

    if (!mask)
        success = TRUE;
    else {
        if (msgtyp == ATOM_INVALID_VALUE  || !xif_root_window_query(&rwin,1) ||
            xif_send_client_message(rwin,msgtyp,0,videoep_unsignd,1,&mask) < 0)
        {
            OHM_ERROR("videoep: can't send videoipc notification to Xserver");
            success = FALSE;
        }
        else {
            OHM_DEBUG(DBG_IPC, "videoipc notification (mask 0x%x) "
                      "sent to Xserver", mask);
            success = TRUE;
        }
    }

    return success;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
