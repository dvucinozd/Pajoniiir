/* Thin wrapper around the S3-side bulk frame builder so the roundtrip test
 * can link both firmware sides without their control_link.h headers (which
 * define the same types) colliding in one translation unit. */

#include "../../firmware/control-board-s3/components/control_link/include/control_link.h"

#include <stdio.h>
#include <string.h>

size_t s3_build_descriptor_frame(uint8_t *out, size_t cap, uint8_t seq,
                                 uint16_t vid, uint16_t pid, uint16_t caps,
                                 const char *product)
{
    ctrl_descriptor_report_t rep;
    memset(&rep, 0, sizeof(rep));
    rep.vid = vid;
    rep.pid = pid;
    rep.caps = caps;
    if (product) {
        snprintf(rep.product, sizeof(rep.product), "%.*s",
                 CTRL_DESC_PRODUCT_MAX, product);
    }
    return ctrl_bulk_build_descriptor_frame(out, cap, seq, &rep);
}
