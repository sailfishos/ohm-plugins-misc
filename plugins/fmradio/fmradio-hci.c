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
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "fmradio-plugin.h"

/*
 * This file is pure black voodoo magic. Take at least a healthy dose
 * of garlic before touching this...
 */


/********************
 * hci_enable
 ********************/
int
hci_enable(fmradio_context_t *ctx)
{
#if 0
    struct hci_filter   flt;
#endif
    int                 ctl, hci, success;
    uint16_t            ocf = 0x00;
    uint8_t             ogf = 0x3f;
    char                hcicmd[] = { 0xf3, 0x88, 0x01, 0x02 };
    int                 devid;

    (void)ctx;

    if ((ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) < 0) {
        OHM_ERROR("fmradio: failed to create HCI control socket (%d: %s).",
                  errno, strerror(errno));
        return FALSE;
    }
    
    success = TRUE;
    if (ioctl(ctl, HCIDEVUP, 0) < 0) {
        if (errno != EALREADY) {
            OHM_ERROR("fmradio: failed to power up bluetooth device (%d: %s).",
                      errno, strerror(errno));
            success = FALSE;
        }
    }
    
    close(ctl);

    if (success) {
        devid = hci_get_route(NULL);

        if ((hci = hci_open_dev(devid)) >= 0) {

#if 0
            /* I guess this is not really needed (since we never wait for
             * a reply and try to read anything)... */
            hci_filter_clear(&flt);
            hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
            hci_filter_all_events(&flt);
            if (setsockopt(hci, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
                OHM_ERROR("fmradio: HCI filter setup failed (%d: %s).",
                          errno, strerror(errno));
                success = FALSE;
            }
#endif       
            
            if (hci_send_cmd(hci, ogf, ocf, sizeof(hcicmd), hcicmd) < 0) {
                OHM_ERROR("fmradio: failed to send HCI command (%d: %s).",
                          errno, strerror(errno));
                success = FALSE;
            }
            
            hci_close_dev(hci);
        }
    }

    return success;
}


/********************
 * hci_disable
 ********************/
int
hci_disable(fmradio_context_t *ctx)
{
#if 0
    struct hci_filter   flt;
#endif
    int                 ctl, hci, success;
    uint16_t            ocf = 0x00;
    uint8_t             ogf = 0x3f;
    char                hcicmd[] = { 0xf3, 0x88, 0x01, 0x01 };
    int                 devid;

    (void)ctx;

    success = TRUE;
    devid = hci_get_route(NULL);

    if ((hci = hci_open_dev(devid)) >= 0) {
#if 0
            /* I guess this is not really needed (since we never wait for
             * a reply and try to read anything)... */
            hci_filter_clear(&flt);
            hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
            hci_filter_all_events(&flt);
            if (setsockopt(hci, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
                OHM_ERROR("fmradio: HCI filter setup failed (%d: %s).",
                          errno, strerror(errno));
                success = FALSE;
            }
#endif       
            
            if (hci_send_cmd(hci, ogf, ocf, sizeof(hcicmd), hcicmd) < 0) {
                OHM_ERROR("fmradio: failed to send HCI command (%d: %s).",
                          errno, strerror(errno));
                success = FALSE;
            }
            
            hci_close_dev(hci);
    }

    if ((ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) < 0) {
        OHM_ERROR("fmradio: failed to create HCI control socket (%d: %s).",
                  errno, strerror(errno));
        return FALSE;
    }
    
    if (ioctl(ctl, HCIDEVDOWN, 0) < 0) {
        if (errno != EALREADY) {
            OHM_ERROR("fmradio: failed to power up bluetooth device (%d: %s).",
                      errno, strerror(errno));
            success = FALSE;
        }
    }
    
    close(ctl);
    
    return success;


#if 0
    struct hci_dev_info di;
    int                 ctl, success;
    
    (void)ctx;

    if ((ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) < 0) {
        OHM_ERROR("fmradio: failed to create HCI control socket (%d: %s).",
                  errno, strerror(errno));
        return FALSE;
    }
    
    if (ioctl(ctl, HCIDEVDOWN, 0) < 0) {
        if (errno != EALREADY) {
            OHM_ERROR("fmradio: failed to power up bluetooth device (%d: %s).",
                      errno, strerror(errno));
            success = FALSE;
        }
        else
            success = TRUE;
    }
    else
        success = TRUE;

    close(ctl);

    return success;
#endif
}





/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
