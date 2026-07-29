/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Project-local ESP-IDF USB DWC channel-interrupt decoder.
 *
 * Why this exists
 * ---------------
 * usb_dwc_hal_chan_decode_intr() asserts that every channel error also raised
 * CHHLTD ("an error should have halted the channel"). The HAL's own contract
 * contradicts that for one case, in its header comment:
 *
 *     "When USB_DWC_LL_INTR_CHAN_BNAINTR occurs,
 *      USB_DWC_LL_INTR_CHAN_CHHLTD will NOT."
 *
 * and BNAINTR is nevertheless a member of CHAN_INTRS_ERROR_MSK. A BNA therefore
 * enters the error branch and trips the assertion. With
 * CONFIG_HAL_DEFAULT_ASSERTION_LEVEL=2 that is a panic and a reboot.
 *
 * This is not theoretical on this board. A 30-second capture on the P4 hit it
 * (docs/DEVELOPMENT_PLAN.md), and docs/bench-notes.md records the trigger
 * precisely: streaming from USB during playback. The original mitigation was to
 * preload each track into PSRAM so playback never touched USB. That mitigation
 * is gone - the bounded compressed cache streams from USB by design - so this
 * decoder is now the only thing standing between a BNA and a reboot mid-set.
 *
 * Why a full reimplementation rather than a delegating wrapper
 * -----------------------------------------------------------
 * The first statement of the real function reads *and clears* the interrupt
 * register. Peeking at the interrupts before delegating to __real_ would leave
 * it nothing to decode, so the logic has to be reproduced here. It is a mirror
 * of the ESP-IDF 6.0.2 implementation with exactly one behavioural difference:
 * BNA without CHHLTD is routed into the HCD's existing recoverable pipe-error
 * path instead of aborting. Every other missing-halt case still aborts, because
 * that genuinely does violate the invariant.
 *
 * Keeping it honest across IDF upgrades
 * ------------------------------------
 * A mirrored implementation silently rots when the original changes. The
 * version assertion below fails the build on any ESP-IDF other than the one
 * this was checked against, so an upgrade forces someone to re-read the
 * upstream function rather than discover the drift on hardware.
 */

#include <stdlib.h>

#include "esp_idf_version.h"
#include "hal/usb_dwc_hal.h"
#include "hal/usb_dwc_ll.h"

#if (ESP_IDF_VERSION_MAJOR != 6) || (ESP_IDF_VERSION_MINOR != 0)
#error "Re-read usb_dwc_hal_chan_decode_intr() in this ESP-IDF before building. \
This file mirrors the 6.0.x implementation and must be verified against any other."
#endif

#define DDJ_DWC_CHAN_ERROR_MASK (USB_DWC_LL_INTR_CHAN_STALL | \
                                 USB_DWC_LL_INTR_CHAN_BBLEER | \
                                 USB_DWC_LL_INTR_CHAN_BNAINTR | \
                                 USB_DWC_LL_INTR_CHAN_XCS_XACT_ERR)

/* Observability for the hardware acceptance run that is still open on this:
 * "reproduce sustained USB playback/storage activity and confirm BNA recovery".
 * Without a counter that check has nothing to look at - a silent recovery and a
 * BNA that never happened are indistinguishable. Plain uint32_t written only
 * from the USB interrupt path and read for diagnostics; a torn read would at
 * worst misreport a count. */
static uint32_t s_bna_recovered_count;

uint32_t usb_dwc_compat_bna_recovered_count(void)
{
    return s_bna_recovered_count;
}

usb_dwc_hal_chan_event_t __wrap_usb_dwc_hal_chan_decode_intr(
    usb_dwc_hal_chan_t *chan_obj)
{
    uint32_t chan_intrs =
        usb_dwc_ll_hcint_read_and_clear_intrs(chan_obj->regs);

    /* Order matters and mirrors upstream: errors > halt request > completed. */
    if (chan_intrs & DDJ_DWC_CHAN_ERROR_MASK) {
        const bool halted = (chan_intrs & USB_DWC_LL_INTR_CHAN_CHHLTD) != 0u;
        const bool bna = (chan_intrs & USB_DWC_LL_INTR_CHAN_BNAINTR) != 0u;
        if (!halted && !bna) {
            /* Upstream's invariant, kept: any error other than BNA really is
             * expected to have halted the channel, and a violation is a bug
             * worth failing loudly for. */
            abort();
        }
        if (!halted && bna) {
            s_bna_recovered_count++;
        }

        usb_dwc_hal_chan_error_t error;
        if (chan_intrs & USB_DWC_LL_INTR_CHAN_STALL) {
            error = USB_DWC_HAL_CHAN_ERROR_STALL;
        } else if (chan_intrs & USB_DWC_LL_INTR_CHAN_BBLEER) {
            error = USB_DWC_HAL_CHAN_ERROR_PKT_BBL;
        } else if (chan_intrs & USB_DWC_LL_INTR_CHAN_BNAINTR) {
            error = USB_DWC_HAL_CHAN_ERROR_BNA;
        } else {   /* USB_DWC_LL_INTR_CHAN_XCS_XACT_ERR */
            error = USB_DWC_HAL_CHAN_ERROR_XCS_XACT;
        }
        chan_obj->error = error;
        chan_obj->flags.active = 0;
        return USB_DWC_HAL_CHAN_EVENT_ERROR;
    }

    if (chan_intrs & USB_DWC_LL_INTR_CHAN_CHHLTD) {
        usb_dwc_hal_chan_event_t event;
        if (chan_obj->flags.halt_requested) {
            chan_obj->flags.halt_requested = 0;
            event = USB_DWC_HAL_CHAN_EVENT_HALT_REQ;
        } else {
            /* Halted by QTD HOC. */
            event = USB_DWC_HAL_CHAN_EVENT_CPLT;
        }
        chan_obj->flags.active = 0;
        return event;
    }

    if (chan_intrs & USB_DWC_LL_INTR_CHAN_XFERCOMPL) {
        /* Transfer complete without a halt: a short interrupt IN packet whose
         * QTD has no HOC bit. Halt manually so the next QTD does not run; the
         * resulting halt interrupt comes back round as the CPLT event. */
        usb_dwc_ll_hcchar_disable_chan(chan_obj->regs);
        return USB_DWC_HAL_CHAN_EVENT_NONE;
    }

    abort();
}
