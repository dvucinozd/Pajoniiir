/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Project-local ESP-IDF v5.5 USB DWC compatibility shim.
 */

#include <stdlib.h>
#include "hal/usb_dwc_hal.h"
#include "hal/usb_dwc_ll.h"

#define DDJ_DWC_CHAN_ERROR_MASK (USB_DWC_LL_INTR_CHAN_STALL | \
                                 USB_DWC_LL_INTR_CHAN_BBLEER | \
                                 USB_DWC_LL_INTR_CHAN_BNAINTR | \
                                 USB_DWC_LL_INTR_CHAN_XCS_XACT_ERR)

usb_dwc_hal_chan_event_t __wrap_usb_dwc_hal_chan_decode_intr(
    usb_dwc_hal_chan_t *chan_obj)
{
    uint32_t chan_intrs =
        usb_dwc_ll_hcint_read_and_clear_intrs(chan_obj->regs);

    /* IDF documents BNAINTR as the sole channel error that does not also
     * raise CHHLTD. Treat it as the normal recoverable BNA pipe error. Keep
     * abort semantics for every other error that violates the HAL invariant. */
    if (chan_intrs & DDJ_DWC_CHAN_ERROR_MASK) {
        bool halted = (chan_intrs & USB_DWC_LL_INTR_CHAN_CHHLTD) != 0u;
        bool bna_without_halt =
            (chan_intrs & USB_DWC_LL_INTR_CHAN_BNAINTR) != 0u;
        if (!halted && !bna_without_halt) {
            abort();
        }

        if (chan_intrs & USB_DWC_LL_INTR_CHAN_STALL) {
            chan_obj->error = USB_DWC_HAL_CHAN_ERROR_STALL;
        } else if (chan_intrs & USB_DWC_LL_INTR_CHAN_BBLEER) {
            chan_obj->error = USB_DWC_HAL_CHAN_ERROR_PKT_BBL;
        } else if (chan_intrs & USB_DWC_LL_INTR_CHAN_BNAINTR) {
            chan_obj->error = USB_DWC_HAL_CHAN_ERROR_BNA;
        } else {
            chan_obj->error = USB_DWC_HAL_CHAN_ERROR_XCS_XACT;
        }
        chan_obj->flags.active = 0;
        return USB_DWC_HAL_CHAN_EVENT_ERROR;
    }

    if (chan_intrs & USB_DWC_LL_INTR_CHAN_CHHLTD) {
        usb_dwc_hal_chan_event_t event;
        if (chan_obj->flags.halt_requested) {
            chan_obj->flags.halt_requested = 0;
            event = USB_DWC_HAL_CHAN_EVENT_HALT_REQ;
        } else {
            event = USB_DWC_HAL_CHAN_EVENT_CPLT;
        }
        chan_obj->flags.active = 0;
        return event;
    }

    if (chan_intrs & USB_DWC_LL_INTR_CHAN_XFERCOMPL) {
        usb_dwc_ll_hcchar_disable_chan(chan_obj->regs);
        return USB_DWC_HAL_CHAN_EVENT_NONE;
    }

    abort();
}
