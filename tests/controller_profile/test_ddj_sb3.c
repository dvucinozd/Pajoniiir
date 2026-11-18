#include "controller_profile.h"
#include "controller_profile_manager.h"
#include "controller_profile_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void refresh_crc(uint8_t *blob, size_t len)
{
    uint32_t crc = cp_crc32(blob + 16, len - 16);
    for (unsigned i = 0; i < 4; ++i) blob[12+i] = (uint8_t)(crc >> (8*i));
}

int main(void)
{
    uint8_t blob[16384], bad[16384];
    FILE *file = fopen("../../controllers/pioneer_ddj_sb3/profile.s3bin", "rb");
    assert(file);
    size_t len = fread(blob, 1, sizeof(blob), file);
    assert(feof(file) && !ferror(file)); fclose(file);
    static cp_profile_t profile;
    controller_profile_meta_t meta;
    assert(cp_profile_parse(blob, len, &profile) == CP_OK);
    assert(controller_profile_meta_parse(blob, len, &meta) == ESP_OK);
    assert(profile.vid == 0x2b73 && profile.pid == 0x0019);
    assert(profile.flags & CP_PF_USB_AUDIO);
    assert(profile.flags & CP_PF_UAC_S16LE);
    assert(!(profile.flags & CP_PF_NATIVE_HEADPHONES));
    /* Parser and storage admission reject 16-bit wire audio on v2/v3 and
     * without the USB audio capability. */
    for (unsigned version = 2; version <= 3; ++version) {
        memcpy(bad, blob, len); bad[4] = (uint8_t)version;
        assert(cp_profile_parse(bad, len, &profile) == CP_ERR_BOUNDS);
        assert(controller_profile_meta_parse(bad, len, &meta) != ESP_OK);
    }
    memcpy(bad, blob, len); bad[20] &= ~CP_PF_USB_AUDIO; refresh_crc(bad, len);
    assert(cp_profile_parse(bad, len, &profile) == CP_ERR_BOUNDS);
    assert(controller_profile_meta_parse(bad, len, &meta) != ESP_OK);
    /* The runtime reports the format only while the profile is active. */
    controller_profile_runtime_init();
    assert(!controller_profile_runtime_uac_s16le());
    assert(controller_profile_runtime_activate(blob, len, 0x2b73, 0x0019));
    assert(controller_profile_runtime_uac_s16le());
    memcpy(bad, blob, len); bad[20] &= ~CP_PF_UAC_S16LE; refresh_crc(bad, len);
    assert(controller_profile_runtime_activate(bad, len, 0x2b73, 0x0019));
    assert(!controller_profile_runtime_uac_s16le());
    assert(controller_profile_runtime_activate(blob, len, 0x2b73, 0x0019));
    controller_profile_runtime_clear();
    assert(!controller_profile_runtime_uac_s16le());
    puts("PASS DDJ-SB3 profile: S16LE wire format flag, parser/storage admission, activation/clear");
    return 0;
}
