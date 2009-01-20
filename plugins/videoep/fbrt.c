typedef struct fbrt_device_s {
    struct fbrt_device_s     *next;
    char                     *name; /* fb device name (i.e. /dev/fb0) */
    int                       idx;  /* plane idx i.e. 0 for  /dev/fb0 */
    int                       fd;
    struct omapfb_mem_info    mem;
} fbrt_device_t;

typedef struct fbrt_config_s {
    struct fbrt_config_s     *next;
    fbrt_device_t            *fbdev;
    struct omapfb_plane_info  plane;
} fbrt_config_t;

typedef struct fbrt_route_s {
    struct fbrt_route_s      *next;
    char                     *name;  /* name of the route (i.e. device name) */
    fbrt_config_t            *confs; /* configurations */
} fbrt_route_t;


static fbrt_device_t *devices;
static fbrt_route_t  *routes;

static fbrt_device_t *find_device(const char *);
static fbrt_route_t  *find_route(const char *);
static int get_plane_idx(const char *);
static unsigned char get_channel_out(const char *);
static void print_plane(const char *, struct omapfb_plane_info *);


static int fbrt_device_new(const char *name, unsigned int memsize)
{
    fbrt_device_t *device = NULL;
    int            fd     = -1;
    int            idx;

    do {
        if (name == NULL) {
            errno = EINVAL;
            break;
        }

        if ((idx = get_plane_idx(name)) < 0) {
            printf("%s(): Can't find plane index for '%s'\n",
                   __FUNCTION__, name);
            errno = EINVAL;
            break;
        }

        if ((device = malloc(sizeof(*device))) == NULL) {
            printf("%s(): memory allocation failure\n", __FUNCTION__);
            break;
        }

        memset(device, 0, sizeof(*device));
   
        if ((fd = open(name, O_WRONLY)) < 0) {
            printf("Can't open '%s'. Reason: %s\n", name, strerror(errno));
            break;
        }

        if (ioctl(fd, OMAPFB_QUERY_MEM, &device->mem) < 0) {
            printf("Can't read memory info of '%s'. Reason: %s\n",
                   name, strerror(errno));
            break;
        }

        
        if (memsize > 0) {
            device->mem.size = memsize;

            if (ioctl(fd, OMAPFB_SETUP_MEM, &device->mem) < 0) {
                printf("Can't set memory of '%s' to %d. Reason: %s\n",
                       name, memsize, strerror(errno));
                break;
            }
            
            printf("videoep: memory set %d byte on '%s' "
                   "succedded\n", memsize, name);
        }
    
        device->next = devices;
        device->name = strdup(name);
        device->idx  = idx;
        device->fd   = fd;
       
        devices = device;

        return 0;               /* everything went OK */

    } while(0);

    /* some error occured */
    if (fd >= 0)
        close(fd);

    if (device != NULL) {
        free(device->name);
        free(device);
    }

    return -1;
}


static int fbrt_route_new(const char *name)
{
    fbrt_route_t *route;

    do {
        if (name == NULL) {
            errno = EINVAL;
            break;
        }

        if ((route = malloc(sizeof(*route))) == NULL) {
            printf("%s(): memory_allocation failed\n", __FUNCTION__);
            break;
        }

        memset(route, 0, sizeof(*route));

        route->next = routes;
        route->name = strdup(name);

        routes = route;

        return 0;               /* everything went OK */

    } while(0);

    /* some error occured */
    return -1;
}


static int fbrt_config_new_clone(const char *device_name,
                                 const char *route_name,
                                 const char *cloned_device_name,
                                 const char *channel_out,
                                 int enabled,
                                 unsigned int width, unsigned int height,
                                 unsigned int x, unsigned int y)
{
    fbrt_config_t  *config = NULL;
    fbrt_device_t  *device;
    int             clone_idx;
    fbrt_route_t   *route;
    fbrt_config_t  *last;

    do {
        if ((device = find_device(device_name)) == NULL) {
            printf("%s(): Can't find device '%s'\n", __FUNCTION__,device_name);
            break;
        }

        if ((clone_idx = get_plane_idx(cloned_device_name)) < 0) {
            printf("%s(): Can't figure out plane index for device '%s'\n",
                   __FUNCTION__, cloned_device_name);
            break;
        }

        if ((route = find_route(route_name)) == NULL) {
            printf("%s(): Can't find route '%s'\n", __FUNCTION__, route_name);
            break;
        }

        if ((config = malloc(sizeof(*config))) == NULL) {
            printf("%s(): Can't allocate memory\n", __FUNCTION__);
            break;
        }
        
        for (last = (fbrt_config_t*)&route->confs;
             last->next != NULL;
             last = last->next)
            ;

        memset(config, 0, sizeof(*config));

        config->fbdev = device;

        config->plane.pos_x = x;
        config->plane.pos_y = y;
        config->plane.enabled = enabled ? 1 : 0;
        config->plane.channel_out = get_channel_out(channel_out);
        config->plane.mirror = 0;
        config->plane.clone_idx = enabled ? (OMAPFB_CLONE_ENABLED|clone_idx):0;
        config->plane.out_width = width;
        config->plane.out_height = height;

        last->next = config;

        return 0;               /* everything went OK */

    } while(0);

    /* some error occured */
    if (config != NULL) {

        free(config);
    }
    
    return -1;
}

static int fbrt_config_new_route(const char *device_name,
                                 const char *route_name,
                                 const char *channel_out,
                                 int         enabled)
{
    fbrt_config_t  *config = NULL;
    fbrt_device_t  *device;
    fbrt_route_t   *route;
    fbrt_config_t  *last;

    do {
        if ((device = find_device(device_name)) == NULL) {
            printf("%s(): Can't find device '%s'\n", __FUNCTION__,device_name);
            break;
        }

        if ((route = find_route(route_name)) == NULL) {
            printf("%s(): Can't find route '%s'\n", __FUNCTION__, route_name);
            break;
        }

        if ((config = malloc(sizeof(*config))) == NULL) {
            printf("%s(): Can't allocate memory\n", __FUNCTION__);
            break;
        }
        
        for (last = (fbrt_config_t*)&route->confs;
             last->next != NULL;
             last = last->next)
            ;

        memset(config, 0, sizeof(*config));

        if (ioctl(device->fd, OMAPFB_QUERY_PLANE, &config->plane) < 0) {
            printf("Can't read plane info of '%s'\n", device->name);
            break;
        }
        else {
            print_plane(device->name, &config->plane);
        }

        config->fbdev = device;

        config->plane.enabled = enabled ? 1 : 0;
        config->plane.channel_out = get_channel_out(channel_out);

        last->next = config;

        return 0;               /* everything went OK */

    } while(0);

    /* some error occured */
    if (config != NULL) {

        free(config);
    }
    
    return -1;
}

static int fbrt_route_video(const char *devnam, const char *tvstandard)
{
    static fbrt_route_t *current_route = NULL;

    fbrt_route_t  *route;
    fbrt_config_t *config;
    fbrt_device_t *device;
    int            success = 1;

    printf("%s(): enter\n", __FUNCTION__);

    if ((route = find_route(devnam)) == NULL)
        return 0;

    printf("Found route\n");
    
    if (route == current_route) {
        printf("videoep: already routed to '%s'\n", devnam);
        return 1;
    }

    for (config = route->confs;   config != NULL;  config = config->next) {
        device = config->fbdev;

        print_plane(device->name, &config->plane);

        if (ioctl(device->fd, OMAPFB_SETUP_PLANE, &config->plane) < 0) {
            printf("ERROR: failed to config plane on '%s'. Reason: %s\n",
                   device->name, strerror(errno));
            success = 0;
        }
    }

    if (success)
        current_route = route;

    return success;
}

static fbrt_device_t *find_device(const char *name)
{
    fbrt_device_t *device;

    if (name == NULL)
        device = NULL;
    else {
        for (device = devices;   device != NULL;   device = device->next) {
            if (device->name != NULL && !strcmp(name, device->name))
                break;
        }
    }

    return device;
}

static fbrt_route_t *find_route(const char *name)
{
    fbrt_route_t *route;

    if (name == NULL)
        route = NULL;
    else {
        for (route = routes;   route != NULL;   route = route->next) {
            if (route->name != NULL && !strcmp(name, route->name))
                break;
        }
    }

    return route;
}

static int get_plane_idx(const char *devnam)
{
    int idx = -1;
    const char  *p;

    if (!strncmp(devnam, "/dev/fb", 7)) {
        p = devnam + 7;

        if (p[0] >= '0' && p[0] <= '9' && p[1] == '\0') {
            idx = p[0] - '0';

            if (idx > OMAPFB_CLONE_MASK)
                idx = -1;
        }
    }

    return idx;
}

static unsigned char get_channel_out(const char *channel_out)
{
    if (channel_out != NULL) {
        if (!strcasecmp(channel_out, "LCD")) {
            return OMAPFB_CHANNEL_OUT_LCD;
        }
        else if (!strcasecmp(channel_out, "DIGIT")) {
            return OMAPFB_CHANNEL_OUT_DIGIT;
        }
    }

    printf("Invalid channel out: '%s'. defaulting to LCD\n", channel_out);

    return OMAPFB_CHANNEL_OUT_LCD;
}

static void print_plane(const char *devnam, struct omapfb_plane_info *plane)
{
    printf("videoep: setting plane info on '%s'\n", devnam);
    printf("    pos_x      : %d\n", plane->pos_x);
    printf("    pos_y      : %d\n", plane->pos_y);
    printf("    enabled    : %d\n", plane->enabled);
    printf("    channel_out: %d\n", plane->channel_out);
    printf("    mirror     : %d\n", plane->mirror);
    printf("    clone_idx  : %d\n", plane->clone_idx);
    printf("    out_width  : %d\n", plane->out_width);
    printf("    out_height : %d\n", plane->out_height);
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
