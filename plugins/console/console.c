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


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>
#include <gmodule.h>

#include <ohm/ohm-plugin.h>

#define BUSY_MESSAGE "Only a single console allowed and is already active.\n"

#define INVALID_ID   -1
#define BUFFER_CHUNK 128

#define CALLBACK(c, cb, args...) do {               \
        if ((c)->cb != NULL) {                      \
            (c)->active++;                          \
            (c)->cb((c)->gid, ## args);             \
            (c)->active--;                          \
        }                                           \
    } while (0)

#define CLOSED(c) (!(c)->active && (c)->flags & CONSOLE_CLOSED)


#ifndef ALLOC
#undef ALLOC
#undef ALLOC_OBJ
#undef ALLOC_ARR
#undef REALLOC_ARR
#undef STRDUP
#undef FREE

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
                if ((n) > (o))                                          \
                    memset(__ptr + (o), 0, ((n)-(o)) * sizeof(*ptr));   \
                ptr = __ptr;                                            \
            }                                                           \
            __ptr; })
                
#define FREE(obj) do { if (obj) free(obj); } while (0)

#define STRDUP(s) ({                                    \
            char *__s = s;                              \
            __s = ((s) ? strdup(s) : strdup(""));       \
            __s; })
#endif


#define DEBUG(fmt, args...) do {                                        \
        printf("[%s] "fmt"\n", __FUNCTION__, ## args);                  \
    } while (0)



enum {
    CONSOLE_NONE     = 0x00,
    CONSOLE_SINGLE   = 0x00,
    CONSOLE_MULTIPLE = 0x01,
    CONSOLE_CLOSED   = 0x02,
};

typedef struct console_s console_t;


#define MAX_GRABS 4                      /* 4 is enough for everybody... */

struct console_s {
    console_t *parent;                   /* where we got accept(2)ed */
    int        nchild;                   /* number of children */
    int        flags;                    /* misc. flags */
    int        active;

    char      *endpoint;                 /* address:port to listen(2) on */
    int        sock;                     /* socket */
    char      *buf;                      /* input buffer */
    size_t     size;                     /* buffer size */
    size_t     used;                     /* buffer used */
    
    void (*opened)(int, struct sockaddr *, int); /* open callback */
    void (*closed)(int);                         /* close callback */
    void (*input) (int, char *, void *);         /* input callback */
    void  *data;                                 /* opaque callback data */

    GIOChannel *gio;                     /* associated I/O channel */
    guint       gid;                     /* glib source id */

    int         grabs[MAX_GRABS];        /* grabbed file descriptors */
};

#define GRAB_FD(g)      ((g) & 0xffff)
#define GRAB_ID(g)      ((g) >> 16)
#define GRABBED(id, fd) (((id) << 16) | (fd))

static console_t    **consoles;
static unsigned int   nconsole;

static gboolean console_accept (GIOChannel *, GIOCondition, gpointer);
static gboolean console_handler(GIOChannel *, GIOCondition, gpointer);

static int grab_fd  (int fd, int sock);
static int ungrab_fd(int grab);


/*****************************************************************************
 *                       *** initialization & cleanup ***                    *
 *****************************************************************************/

/**
 * plugin_init:
 **/
static void
plugin_init(OhmPlugin *plugin)
{
    (void)plugin;
    return;
}


/**
 * plugin_exit:
 **/
static void
plugin_exit(OhmPlugin *plugin)
{
    (void)plugin;
    return;
}


/********************
 * new_console
 ********************/
static console_t *
new_console(void)
{
    unsigned int i;

    for (i = 0; i < nconsole; i++)
        if (consoles[i] == NULL)
            return ALLOC_OBJ(consoles[i]);
        else if(consoles[i]->sock < 0) {
            memset(consoles[i], 0, sizeof(*consoles[i]));
            return consoles[i];
        }
    
    if (REALLOC_ARR(consoles, nconsole, nconsole + 1) == NULL)
        return NULL;
    nconsole++;

    return ALLOC_OBJ(consoles[i]);
}


/********************
 * del_console
 ********************/
static void
del_console(console_t *c)
{
    unsigned int i;

    for (i = 0; i < MAX_GRABS; i++) {
        if (c->grabs[i] != 0) {
            ungrab_fd(c->grabs[i]);
            c->grabs[i] = 0;
        }
    }

    FREE(c->endpoint);
    close(c->sock);
    
    c->endpoint = NULL;
    c->sock     = -1;
    c->used     = 0;
    memset(c->buf, 0, c->size);

    if (c->nchild > 0) {
        for (i = 0; i < nconsole; i++) {
            if (consoles[i] == NULL || consoles[i]->sock < 0)
                continue;
            if (consoles[i]->parent == c) {
                consoles[i]->parent = NULL;
                c->nchild--;
            }
        }
    }
    if (c->parent != NULL)
        c->parent->nchild--;
}


/********************
 * lookup_console
 ********************/
static console_t *
lookup_console(int id)
{
    unsigned int i;

    for (i = 0; i < nconsole; i++)
        if (consoles[i] != NULL && consoles[i]->gid == (guint)id)
            return consoles[i];
    
    return NULL;
}


/*****************************************************************************
 *                           *** exported methods ***                        *
 *****************************************************************************/

/********************
 * console_open
 ********************/
OHM_EXPORTABLE(int, console_open, (char *address,
                                   void (*opened)(int, struct sockaddr *, int),
                                   void (*closed)(int),
                                   void (*input)(int, char *, void *),
                                   void  *data, int multiple))
{
    console_t          *c = NULL;
    struct sockaddr_in  sin;
    char                addr[64], *portp, *end;
    int                 len, reuse, flags;
    GIOCondition        events;

    if ((portp = strchr(address, ':')) == NULL)
        return -1;
    
    len = (int)portp - (int)address;
    strncpy(addr, address, len);
    addr[len] = '\0';
    portp++;

    memset(&sin, 0, sizeof(sin));

    sin.sin_family = AF_INET;
    if (!inet_aton(addr, &sin.sin_addr))
        return -1;
    
    sin.sin_port = strtoul(portp, &end, 10);
    sin.sin_port = htons(sin.sin_port);
    if (*end != '\0')
        return -1;
    
    if ((c = new_console()) == NULL)
        return -1;

    if ((c->sock = socket(sin.sin_family, SOCK_STREAM, 0)) < 0)
        return -1;

    c->endpoint = STRDUP(address);
    c->opened   = opened;
    c->closed   = closed;
    c->input    = input;
    c->data     = data;
    c->flags    = multiple ? CONSOLE_MULTIPLE : CONSOLE_SINGLE;
    
    reuse = 1;
    setsockopt(c->sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if ((flags = fcntl(c->sock, F_GETFD, 0)) == 0) {
        flags |= FD_CLOEXEC;
        fcntl(c->sock, F_SETFD, flags);
    }
    
    if (c->endpoint == NULL || c->sock < 0)
        goto fail;
    
    if (bind(c->sock, (struct sockaddr *)&sin, sizeof(sin)) != 0 ||
        listen(c->sock, 2) != 0)
        goto fail;
    
    if ((c->gio = g_io_channel_unix_new(c->sock)) == NULL)
        goto fail;
    
    events = G_IO_IN | G_IO_HUP;
    return (c->gid = g_io_add_watch(c->gio, events, console_accept, c));
    
 fail:
    if (c != NULL) {
        FREE(c->endpoint);
        if (c->sock >= 0)
            close(c->sock);
        c->endpoint = NULL;
        c->sock     = -1;
    }
    return -1;
}


/********************
 * shutdown_console
 ********************/
static void
shutdown_console(console_t *c)
{
    g_source_remove(c->gid);
    g_io_channel_unref(c->gio);
    del_console(c);
}

/********************
 * console_close
 ********************/
OHM_EXPORTABLE(int, console_close, (int id))
{
    console_t *c = lookup_console(id);

    if (c == NULL)
        return EINVAL;

    if (!c->active)
        shutdown_console(c);
    else
        c->flags |= CONSOLE_CLOSED;
    
    return 0;
}


/********************
 * console_write
 ********************/
OHM_EXPORTABLE(int, console_write, (int id, char *buf, size_t size))
{
    console_t *c = lookup_console(id);

    if (c == NULL)
        return EINVAL;

    if (size == 0)
        size = strlen(buf);

    return write(c->sock, buf, size);
}


/********************
 * console_printf
 ********************/
OHM_EXPORTABLE(int, console_printf, (int id, char *fmt, ...))
{
    console_t *c = lookup_console(id);
    int        fd, len;
    FILE      *fp;
    va_list    ap;

    if (c == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if ((fd = dup(c->sock)) < 0)
        return -1;
    
    if ((fp = fdopen(fd, "w")) == NULL) {
        close(fd);
        return -1;
    }
    
    va_start(ap, fmt);
    len = vfprintf(fp, fmt, ap);
    va_end(ap);
    fflush(fp);
    fclose(fp);

    return len;
}


/********************
 * grab_fd
 ********************/
static int
grab_fd(int fd, int sock)
{
    int gid = fd;
    int gfd = dup(fd);

    if (gfd < 0)
        return 0;
    
    if (dup2(sock, fd) < 0) {
        close(gfd);
        return 0;
    }

    if (fd == fileno(stdout))
        setvbuf(stdout, NULL, _IONBF, 0);
    else if (fd == fileno(stderr))
        setvbuf(stderr, NULL, _IONBF, 0);
    
    return GRABBED(gid, gfd);
}


/********************
 * ungrab_fd
 ********************/
static int
ungrab_fd(int grab)
{
    int gid = GRAB_ID(grab);
    int gfd = GRAB_FD(grab);

    if (gfd < 0)
        return ENOENT;

    if (gid != gfd) {
        dup2(gfd, gid);
        close(gfd);
    }

    if (gid == fileno(stdout))
        setvbuf(stdout, NULL, _IOLBF, 0);
    else if (gid == fileno(stderr))
        setvbuf(stderr, NULL, _IOLBF, 0);
    
    return 0;
}


/********************
 * console_grab
 ********************/
OHM_EXPORTABLE(int, console_grab, (int id, int fd))
{
    console_t *c = lookup_console(id);
    int        i, empty;

    if (c == NULL)
        return EINVAL;
    
    for (i = 0, empty = -1; i < MAX_GRABS; i++) {
        if (c->grabs[i] != 0) {
            if (GRAB_ID(c->grabs[i]) == fd)
                return EBUSY;
        }
        else
            if (empty < 0)
                empty = i;
    }

    if (empty < 0)
        return ENOSPC;
    
    if ((c->grabs[empty] = grab_fd(fd, c->sock)) == 0)
        return errno;

    return 0;
}


/********************
 * console_ungrab
 ********************/
OHM_EXPORTABLE(int, console_ungrab, (int id, int fd))
{
    console_t *c = lookup_console(id);
    int        i;

    if (c == NULL)
        return EINVAL;

    if (fd < 0) {
        for (i = 0; i < MAX_GRABS; i++) {
            if (c->grabs[i] != 0) {
                ungrab_fd(c->grabs[i]);
                c->grabs[i] = 0;
            }
        }
        return 0;
    }
    else {
        int entry = -1;
        for (i = 0; i < MAX_GRABS; i++) {
            if (GRAB_ID(c->grabs[i]) == fd) {
                entry = i;
                break;
            }
        }
        if (entry >= 0) {
            ungrab_fd(c->grabs[entry]);
            c->grabs[entry] = 0;
            return 0;
        }
        else
            return ENOENT;
    }
}




#if 0
/********************
 * console_grab
 ********************/
OHM_EXPORTABLE(int, console_grab, (int id, int fd))
{
    console_t *c = lookup_console(id);

    if (c->grabfd >= 0) {
        dup2(c->grabfd, c->grabid);
        c->grabfd = c->grabid = -1;
    }

    c->grabid = fd;
    c->grabfd = dup(fd);
    
    if (c->grabfd < 0)
        return errno;

    if (dup2(c->sock, fd) < 0) {
        close(c->grabfd);
        c->grabfd = -1;
        return errno;
    }
    
    return (c->grabfd < 0 ? errno : 0);
}


/********************
 * console_ungrab
 ********************/
OHM_EXPORTABLE(int, console_ungrab, (int id))
{
    console_t *c = lookup_console(id);
    
    if (c == NULL)
        return EINVAL;

    if (c->grabfd < 0)
        return ENOENT;

    dup2(c->grabfd, c->grabid);
    c->grabfd = c->grabid = -1;

    return 0;
}
#endif


/*****************************************************************************
 *                       *** misc. helper functions ***                      *
 *****************************************************************************/

/********************
 * console_accept
 ********************/
static gboolean
console_accept(GIOChannel *source, GIOCondition condition, gpointer data)
{
    console_t          *lc = (console_t *)data;
    console_t          *c;
    struct sockaddr_in  addr;
    socklen_t           addrlen = sizeof(addr);
    int                 sock, flags;

    (void)source;

    if (condition != G_IO_IN)
        return TRUE;
    
    if ((sock = accept(lc->sock, (struct sockaddr *)&addr, &addrlen)) < 0)
        return TRUE;
    
    if ((flags = fcntl(lc->sock, F_GETFD, 0)) == 0) {
        flags |= FD_CLOEXEC;
        fcntl(lc->sock, F_SETFD, flags);
    }
    
    if (lc->nchild > 1 && !(lc->flags & CONSOLE_MULTIPLE)) {
        write(sock, BUSY_MESSAGE, sizeof(BUSY_MESSAGE) - 1);
        close(sock);
        return TRUE;
    }
    
    if ((c = new_console()) == NULL)
        goto fail;

    c->endpoint = STRDUP(lc->endpoint);
    c->size     = BUFFER_CHUNK;
    c->used     = 0;
    c->buf      = ALLOC_ARR(char, c->size);
    c->sock     = sock;
    c->opened   = lc->opened;
    c->closed   = lc->closed;
    c->input    = lc->input;
    c->data     = lc->data;

    if (c->endpoint == NULL || c->buf == NULL)
        goto fail;

    if ((c->gio = g_io_channel_unix_new(c->sock)) == NULL)
        goto fail;
    
    c->parent = lc;
    lc->nchild++;

    c->gid = g_io_add_watch(c->gio, G_IO_IN | G_IO_HUP, console_handler, c);
    
    CALLBACK(c, opened, (struct sockaddr *)&addr, (int)addrlen);
    if (CLOSED(c))
        shutdown_console(c);

    return TRUE;

 fail:
    if (sock >= 0)
        close(sock);
    if (c) {
        FREE(c->endpoint);
        c->endpoint = NULL;
        c->sock     = -1;
    }

    return TRUE;
}


/********************
 * console_read
 ********************/
static int
console_read(console_t *c)
{
    int n, left = c->size - c->used - 1;
    
    if (left < BUFFER_CHUNK) {
        if (REALLOC_ARR(c->buf, c->size, c->size + BUFFER_CHUNK) == NULL)
            return -ENOMEM;
        c->size += BUFFER_CHUNK;
    }

    switch ((n = read(c->sock, c->buf + c->used, left))) {
    case  0:
    case -1:
        return n;
    default:
        c->used += n;
        return c->used;
    }
}


/********************
 * console_handler
 ********************/
static gboolean
console_handler(GIOChannel *source, GIOCondition condition, gpointer data)
{
    console_t    *c = (console_t *)data;
    int           left = c->size - c->used - 1;
    unsigned int  i;

    (void)source;

    if (condition & G_IO_IN) {
        switch (console_read(c)) {
        case -1:
            break;
        case 0:
            goto closed;
        default:
            for (i = 0; i < c->used; i++)
                if (c->buf[i] == '\r') {
                    c->buf[i] = '\0';
                    if (i < c->used && c->buf[i+1] == '\n') {
                        i++;
                        c->buf[++i] = '\0';
                    }
                    CALLBACK(c, input, c->buf, c->data);
                    if (CLOSED(c))
                        goto closed;
                    if ((left = c->used - i - 1) > 0) {
                        memmove(c->buf, c->buf + i + 1, left);
                        c->used         -= i + 1;
                        c->buf[c->used]  = '\0';
                        i                = 0;
                    }
                    else
                        c->used = 0;
                }
        }
    }
    
    if (condition & G_IO_HUP) {
        CALLBACK(c, closed);
        goto closed;
    }

    return TRUE;

 closed:
    shutdown_console(c);
    return FALSE;
}


OHM_PLUGIN_DESCRIPTION("console",
                       "0.0.0",
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_LGPL,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_PROVIDES_METHODS(console, 6,
    OHM_EXPORT(console_open  , "open"  ),
    OHM_EXPORT(console_close , "close" ),
    OHM_EXPORT(console_write , "write" ),
    OHM_EXPORT(console_printf, "printf"),
    OHM_EXPORT(console_grab  , "grab"),
    OHM_EXPORT(console_ungrab, "ungrab")
);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

