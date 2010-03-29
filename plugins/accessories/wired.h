#ifndef __WIRED_H__
#define __WIRED_H__

#include <ohm/ohm-plugin.h>


#define FACT_NAME_DEV_ACCESSIBLE "com.nokia.policy.audio_device_accessible"
#define DEVICE_MODE_ECI          "eci"
#define DEVICE_MODE_DEFAULT      "default"
#define ECI_MEMORY_PATH          "/sys/devices/platform/ECI_accessory.0/memory"



void wired_init(OhmPlugin *plugin, int dbg_wired);
void wired_exit(void);

#endif /* __WIRED_H__ */
