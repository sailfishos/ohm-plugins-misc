#ifndef __OHM_VIDEOEP_RANDR_H__
#define __OHM_VIDEOEP_RANDR_H__

#define RANDR_SCREEN_QUERIED      0x01
#define RANDR_SIZE_QUERIED        0x02
#define RANDR_OUTPUT_QUERIED      0x04
#define RANDR_PROPLIST_QUERIED    0x08
#define RANDR_PROPERTIES_QUERIED  0x10

typedef void (*randr_statecb_t)(int, void *);


typedef struct {
    struct randr_screen_s *screen;
    int                    ready;
    int                    sync;
    uint32_t               xid;
    int32_t                x;
    int32_t                y;
    uint32_t               width;
    uint32_t               height;
    uint32_t               mode;
    uint32_t               rotation;
    int                    noutput;
    uint32_t              *outputs;
    int                    npossible;
    uint32_t              *possibles;
} randr_crtc_t;

typedef struct {
    uint32_t               start;
    uint32_t               end;
    uint32_t               total;
} randr_sync_t;

typedef struct {
    struct randr_screen_s *screen;
    int                    ready;
    int                    sync;
    uint32_t               xid;
    char                  *name;
    uint32_t               width;
    uint32_t               height;
    uint32_t               clock;
    randr_sync_t           hsync;
    randr_sync_t           vsync;
    uint32_t               hskew;
    uint32_t               flags;
} randr_mode_t;

/* the enumeration supposed to be
   identical to the xif counterpart */
typedef enum {
    randr_unknown = 0,
    randr_connected,
    randr_disconnected
} randr_connstate_t;

typedef struct {
    struct randr_screen_s *screen;
    int                    queried;
    int                    ready;
    int                    sync;
    uint32_t               xid;
    char                  *name;
    randr_connstate_t      state;
    uint32_t               crtc;
    uint32_t               mode;
    int                    nmode;
    uint32_t              *modes;
} randr_output_t;

typedef struct randr_screen_s {
    int                    ready;
    int                    sync;
    int                    queried;
    uint32_t               rootwin;
    uint32_t               tstamp;
    int                    ncrtc;
    randr_crtc_t          *crtcs;
    int                    noutput;
    randr_output_t        *outputs;
    int                    nmode;
    randr_mode_t          *modes;
} randr_screen_t;



/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

void randr_init(OhmPlugin *);
void randr_exit(OhmPlugin *);
              
int randr_add_state_callback(randr_statecb_t, void *);
int randr_remove_state_callback(randr_statecb_t, void *);

void randr_crtc_set_mode(int, int, char *);
void randr_crtc_set_outputs(int, int, int, char **);

void randr_synchronize(void);

#endif /* __OHM_VIDEOEP_RANDR_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
