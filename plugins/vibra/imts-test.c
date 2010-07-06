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

#include <ImmVibeCore.h>
#include <ImmVibeOS.h>
#include <ImmVibe.h>

#define fatal(fmt, args...) do {                                \
        fprintf(stderr, "fatal error: "fmt"\n" , ## args);      \
        exit(1);                                                \
    } while (0)

#define error(fmt, args...) do {                                \
        fprintf(stderr, "fatal error: "fmt"\n" , ## args);      \
        exit(1);                                                \
    } while (0)

#define info(fmt, args...) do {                                 \
        fprintf(stdout, fmt"\n" , ## args);                     \
    } while (0)

#define usage(bin) do {                                             \
        fprintf(stderr, "usage: %s pattern [... pattern]\n", bin);  \
        exit(0);                                                    \
    } while (0)

#ifndef TRUE
#  undef FALSE
#  define FALSE 0
#  define TRUE (!FALSE)
#endif

void immts_init(void);
void immts_exit(void);
void immts_play(const char *);


VibeInt32 dev = VIBE_INVALID_DEVICE_HANDLE_VALUE;



int main(int argc, char *argv[])
{
    int i;
    
    if (argc < 2) {
        fatal("no pattern given");
        usage(argv[0]);
    }

    
    immts_init();
    for (i = 1; i < argc; i++)
        immts_play(argv[i]);
    immts_exit();
    
    return 0;
}


void
immts_init(void)
{
    VibeStatus status;
    VibeInt32  count, i, state;
    char       name[64];

    status = ImmVibeInitialize(VIBE_CURRENT_VERSION_NUMBER);
    if (VIBE_FAILED(status)) {
        error("vibra: failed to initialize TouchSense library");
        return;
    }

    count = (VibeStatus)ImmVibeGetDeviceCount();
    info("found %d devices", count);
    
    for (i = 0; i < count; i++) {
        status = ImmVibeGetDeviceCapabilityString(i,
                                                  VIBE_DEVCAPTYPE_DEVICE_NAME,
                                                  sizeof(name),
                                                  name);
        status = ImmVibeGetDeviceState(i, &state);
        if (!VIBE_FAILED(status))
            info("#%d. %s: %sattached", i, name,
                 state & VIBE_DEVICESTATE_ATTACHED ? "" : "not ");
    }
    
    
    status = ImmVibeOpenDevice(0, &dev);
    if (VIBE_FAILED(status)) {
        error("vibra: failed to open TouchSense device");
    }

    ImmVibeSetDevicePropertyBool(dev, VIBE_DEVPROPTYPE_DISABLE_EFFECTS,
                                 FALSE);
}


void
immts_exit(void)
{
    ImmVibeCloseDevice(dev);
    ImmVibeTerminate();
    dev = VIBE_INVALID_DEVICE_HANDLE_VALUE;
}


void
immts_play(const char *path)
{
    VibeInt32  duration, magnitude, period;
    VibeInt32  style;
    VibeInt32  risetime, riselevel, fadetime, fadelevel;
    VibeInt32  effectid;
    VibeStatus status;
    
    (void)path;

    duration  = 5000;
    magnitude = VIBE_MAX_MAGNITUDE;
    period    = 50;
    style     = VIBE_STYLE_STRONG;
    risetime  = 1000;
    riselevel = VIBE_MAX_MAGNITUDE / 3;
    fadetime  = 1000;
    fadelevel = VIBE_MAX_MAGNITUDE / 3;
    effectid  = VIBE_INVALID_EFFECT_HANDLE_VALUE;
    
    status = ImmVibePlayPeriodicEffect(dev,
                                       duration, magnitude, period, style,
                                       risetime, riselevel,
                                       fadetime, fadelevel,
                                       &effectid);
    
    if (VIBE_FAILED(status)) {
        error("failed to play effect");
    }

    sleep(2);
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
