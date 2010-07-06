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


static gboolean xevent_cb(GIOChannel *, GIOCondition, gpointer);



static  xrandrt_t *xrandrt_init(const char *display_name)
{
    xrandrt_t *xr;
    int        fd;
    Display   *disp;

    disp = XOpenDisplay(display_name);


    if ((xr = malloc(sizeof(*xr))) == NULL) {
        if (disp != NULL)
            XCloseDisplay(disp);
    }
    else {
        fd = XConnectionNumber(disp);

        memset(xr, 0, sizeof(*xr));
        xr->disp  = disp;
        xr->chan  = g_io_channel_unix_new(fd);
        xr->evsrc = g_io_add_watch(xr->chan, G_IO_IN|G_IO_HUP|G_IO_ERR,
                                   xevent_cb,xr);
        
        printf("videoep: xrandr initialized\n");

        {
            Window root;
            Visual *vis;
            int sno;
            unsigned long wp;
            unsigned long bp;
            Window win;
            int depth;
            XSetWindowAttributes attr;
            unsigned long mask;

            root = XDefaultRootWindow(disp);
            sno = XDefaultScreen(disp);
            vis = XDefaultVisual(disp, sno);
            depth = XDefaultDepth(disp, sno);
            bp  = XBlackPixel(disp, sno);
            wp  = XWhitePixel(disp, sno);

            mask = CWBackPixel | CWBorderPixel | CWEventMask;
            attr.background_pixel = wp;
            attr.border_pixel = bp;
            attr.event_mask = ButtonPressMask | FocusChangeMask;

            win = XCreateWindow(disp, root, 50,50, 400,300, 1, depth,
                                InputOutput, vis, mask, &attr);
            XMapWindow(disp,win);            
            XFlush(disp);
        }
    }

    return xr;
}

static void xrandrt_exit(xrandrt_t *xr)
{
    if (xr != NULL) {
        if (xr->disp != NULL) {
            g_source_remove(xr->evsrc);
            g_io_channel_unref(xr->chan);

            XCloseDisplay(xr->disp);
        }

        free(xr);
    }
}


static gboolean xevent_cb(GIOChannel *ch, GIOCondition cond, gpointer data)
{
    xrandrt_t *xr = (xrandrt_t *)data;
    int        i;
    XEvent     ev;
    gboolean   retval;

    printf("videoep: %s() enter\n", __FUNCTION__);

    if (cond & (G_IO_ERR | G_IO_HUP)) {
        printf("videoep: X server is gone\n");

        g_io_channel_unref(xr->chan);

        if (!(cond & G_IO_HUP)) 
            XCloseDisplay(xr->disp);

        xr->disp  = NULL;
        xr->chan  = NULL;
        xr->evsrc = 0;

        retval = FALSE;
    }
    else {
        do {
            XNextEvent(xr->disp, &ev);
            printf("videoep: received event %d\n", ev.type);
        } while (XPending(xr->disp) > 0);

        retval = TRUE;
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
