
static int       setup_notify_connection(notify_t *, unsigned short);
static void      teardown_notify_connection(notify_t *);
static int       dres_rotation_notify(notify_t *, int);
static int       parse_rotation_notification(char *);
static gboolean  notify_cb(GIOChannel *, GIOCondition, gpointer);



static notify_t *notify_init(unsigned short port)
{
    notify_t *notif;

    if ((notif = malloc(sizeof(*notif))) == NULL)
        return NULL;

    memset(notif, 0, sizeof(*notif));

    if (setup_notify_connection(notif, port) < 0) {
        free(notif);
        return NULL;
    }

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

    if (sockfd < 0)
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

static int parse_rotation_notification(char *msg)
{
    static char keywd[] = "rotation-transition";

    int   kwdlen  = sizeof(keywd) - 1;
    int   transit = -1;
    char *p;

    if (!strncmp(msg, keywd, kwdlen)) {
        for (p = msg + kwdlen;   *p && *p == ' ';   p++)
            ;

        if (!strcmp(p, "on"))
            transit = 1;
        else if (!strcmp(p, "off"))
            transit = 0;
    }

    return transit;
}


static gboolean notify_cb(GIOChannel *ch, GIOCondition cond, gpointer data)
{
    notify_t  *notif = (notify_t *)data;
    gboolean   retval;
    char       buf[156];
    int        len;
    int        transit;

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

            if (len > 0) {
                buf[len] = '\0';

                while (buf[--len] == '\0') ;
                if (buf[len] == '\n') buf[len] = '\0';

                if ((transit = parse_rotation_notification(buf)) >= 0) {

                    OHM_DEBUG(DBG_INFO, "rotation-transition: %d", transit);

                    if (transit !=  notif->transit) {
                        dres_rotation_notify(notif, transit);
                        notif->transit = transit;
                    }
                }
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
