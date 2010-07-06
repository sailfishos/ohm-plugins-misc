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


#define STARTUP_BLOCK_TIME  (2 * 60) /* 2 minutes */

static int       setup_notify_connection(notify_t *, unsigned short);
static void      teardown_notify_connection(notify_t *);
static int       dres_systemui_notify(notify_t *, int);
static int       dres_callui_notify(notify_t *, int);
static int       dres_rotation_notify(notify_t *, int);
static int       parse_systemui_notification(char *, int *);
static int       parse_callui_notification(char *, int *);
static int       parse_rotation_notification(char *, int *);
static gboolean  notify_cb(GIOChannel *, GIOCondition, gpointer);



static notify_t *notify_init(unsigned short port)
{
    notify_t *notif;
    struct timespec tp;

    if ((notif = malloc(sizeof(*notif))) == NULL)
        return NULL;

    memset(notif, 0, sizeof(*notif));

    if (setup_notify_connection(notif, port) < 0) {
        free(notif);
        return NULL;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &tp) < 0)
        notif->start = time(NULL);
    else
        notif->start = tp.tv_sec;

    return notif;
}

static void notify_exit(notify_t *notif)
{
    if (notif != NULL) {
        teardown_notify_connection(notif);
        free(notif);
    }
}

static int setup_notify_connection(notify_t *notif, unsigned short port)
{
    static int             on = 1;

    int                    sockfd;
    struct sockaddr_in     sin;
    const struct sockaddr *saddr;
    socklen_t              addrlen;
    GIOChannel            *chan;
    guint                  evsrc;

    do {  /* not a loop */

        if ((sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
            break;
        
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
            break;

        sin.sin_family = AF_INET;
        sin.sin_port   = htons(port);
        sin.sin_addr.s_addr = inet_addr(VIDEOEP_NOTIFICATION_ADDRESS);

        saddr   = (struct sockaddr *)&sin;
        addrlen = sizeof(sin);

        if (bind(sockfd, saddr, addrlen) < 0)
            break;

        if ((chan = g_io_channel_unix_new(sockfd)) == NULL)
            break;

        evsrc = g_io_add_watch(chan, G_IO_IN|G_IO_HUP|G_IO_ERR,
                               notify_cb, notif);

        notif->port   = port;
        notif->sockfd = sockfd;
        notif->chan   = chan;
        notif->evsrc  = evsrc;

        return 0;

    } while(0);

    OHM_ERROR("Failed to setup server port %d to receive info messages. "
              "Reason: %s", port, strerror(errno));

    if (sockfd >= 0)
        close(sockfd);

    return -1;
}

static void teardown_notify_connection(notify_t *notif)
{
    if (notif != NULL) {
        if (notif->evsrc)
            g_source_remove(notif->evsrc);

        if (notif->chan)
            g_io_channel_unref(notif->chan);

        if (notif->sockfd >= 0)
            close(notif->sockfd);

        notif->sockfd = -1;
        notif->chan   = NULL;
        notif->evsrc  = 0;
    }
}

static int dres_systemui_notify(notify_t *notif, int popup)
{
#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)

    (void)notif;

    char *target;
    char *vars[10];
    int   i;
    int   status;

    target = popup ? "systemui_popup" : "systemui_popdown";

    vars[i=0] = NULL;

    status = resolve(target, vars);

    return status;

#undef DRESIF_VARVALUE
#undef DRESIF_VARTYPE
}

static int dres_callui_notify(notify_t *notif, int popup)
{
#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)

    (void)notif;

    char *target;
    char *vars[10];
    int   i;
    int   status;

    target = popup ? "callui_popup" : "callui_popdown";

    vars[i=0] = NULL;

    status = resolve(target, vars);

    return status;

#undef DRESIF_VARVALUE
#undef DRESIF_VARTYPE
}

static int dres_rotation_notify(notify_t *notif, int transit)
{
#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)

    (void)notif;

    char *target;
    char *vars[10];
    int   i;
    int   status;

    target = transit ? "rotation_start" : "rotation_end";

    vars[i=0] = NULL;

    status = resolve(target, vars);

    return status;

#undef DRESIF_VARVALUE
#undef DRESIF_VARTYPE
}

static int parse_systemui_notification(char *msg, int *length)
{
    static char keywd[] = "systemui ";

    int kwdlen  = sizeof(keywd) - 1;
    int popup   = -1;

    *length = 0;

    if (!strncmp(msg, keywd, kwdlen)) {
        msg += kwdlen;

        if (!strncmp(msg, "popup", 5)) {
            popup = 1;
            *length = kwdlen + 5 + (msg[5] == '\n' ? 1 : 0);
        }
        else if (!strncmp(msg, "popdown", 7)) {
            popup = 0;
            *length = kwdlen + 7 + (msg[7] == '\n' ? 1 : 0);
        }
    }

    return popup;
}

static int parse_callui_notification(char *msg, int *length)
{
    static char keywd[] = "callui ";

    int kwdlen  = sizeof(keywd) - 1;
    int popup   = -1;

    *length = 0;

    if (!strncmp(msg, keywd, kwdlen)) {
        msg += kwdlen;

        if (!strncmp(msg, "popup", 5)) {
            popup = 1;
            *length = kwdlen + 5 + (msg[5] == '\n' ? 1 : 0);
        }
        else if (!strncmp(msg, "popdown", 7)) {
            popup = 0;
            *length = kwdlen + 7 + (msg[7] == '\n' ? 1 : 0);
        }
    }

    return popup;
}

static int parse_rotation_notification(char *msg, int *length)
{
    static char keywd[] = "rotation-transition";

    int   kwdlen  = sizeof(keywd) - 1;
    int   transit = -1;
    char *p;

    *length = 0;

    if (!strncmp(msg, keywd, kwdlen)) {
        for (p = msg + kwdlen;   *p && *p == ' ';   p++)
            ;

        if (!strncmp(p, "on", 2)) {
            transit = 1;
            *length = ((p + 2) - msg) + (p[2] == '\n' ? 1 : 0);
        }
        else if (!strncmp(p, "off", 3)) {
            transit = 0;
            *length = ((p + 3) - msg) + (p[3] == '\n' ? 1 : 0);
        }
    }

    return transit;
}


static gboolean notify_cb(GIOChannel *ch, GIOCondition cond, gpointer data)
{
    notify_t        *notif = (notify_t *)data;
    struct timespec  tp;
    gboolean         retval;
    char             buf[156];
    int              len;
    char            *p, *e;
    int              l;
    time_t           now;
    int              sysui;
    int              callui;
    int              transit;

    if (ch != notif->chan) {
        OHM_ERROR("videoep: %s(): confused with data structures",
                  __FUNCTION__);

        retval = TRUE;
    }
    else {
        if (cond & (G_IO_ERR | G_IO_HUP)) {
            OHM_ERROR("videoep: Network is down");
            teardown_notify_connection(notif);
            retval = FALSE;
        }
        else {
            retval = TRUE;

            for (;;) {
                if ((len = recv(notif->sockfd, buf,sizeof(buf)-1, 0)) < 0) {
                    if (errno == EINTR)
                        continue;
                }
                break;
            }

            if (clock_gettime(CLOCK_MONOTONIC, &tp) < 0)
                now = time(NULL);
            else
                now = tp.tv_sec;


            if ((now - notif->start) > STARTUP_BLOCK_TIME) {
                buf[len] = '\0';

#           if 0
                {
                    char dbgmsg[256];
                    char *f, *t;

                    for (f = buf, t = dbgmsg;  *f;   f++) {
                        if (*f == '\n')     t   += sprintf(t, "<lf>");
                        else if (*f < ' ')  t   += sprintf(t, "<0x%02x>", *f); 
                        else               *t++  = *f;
                    }
                    *t = '\0';
                    
                    OHM_DEBUG(DBG_INFO, "got message: '%s'", dbgmsg);
                }
#           endif

                while (len > 0 && buf[len-1] == '\0')
                    len--;

                for (p = buf;   *p  && p < (buf + len);   p += l) {

                    if ((sysui = parse_systemui_notification(p, &l)) >= 0) {

                        OHM_DEBUG(DBG_INFO, "systemui: %d", sysui);
                        
                        if (sysui != notif->sysui) {
                            dres_systemui_notify(notif, sysui);
                            notif->sysui = sysui;
                        }
                    }
                    else if ((callui = parse_callui_notification(p, &l)) >= 0){

                        OHM_DEBUG(DBG_INFO, "callui: %d", callui);
                        
                        if (callui != notif->callui) {
                            dres_callui_notify(notif, callui);
                            notif->callui = callui;
                        }
                    }
                    else if ((transit = parse_rotation_notification(p,&l))>=0){
                        
                        OHM_DEBUG(DBG_INFO, "rotation-transition: %d",transit);
                        
                        if (transit !=  notif->transit) {
                            dres_rotation_notify(notif, transit);
                            notif->transit = transit;
                        }
                    }
                    else {
                        for (l = 0;  p[l] != '\0';  l++) {
                            if (p[l] == '\n') {
                                l++;
                                break;
                            }
                        }
                    }

                } /* for */
            }
        }
    }

    return retval;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
