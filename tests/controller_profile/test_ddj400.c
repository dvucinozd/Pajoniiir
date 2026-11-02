#include "controller_profile.h"
#include "controller_profile_manager.h"
#include "control_link.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint8_t blob[16384];
    FILE *file = fopen("../../controllers/pioneer_ddj_400/profile.s3bin", "rb");
    assert(file);
    size_t bytes = fread(blob, 1, sizeof(blob), file);
    assert(!ferror(file) && feof(file)); fclose(file);
    static cp_profile_t profile;
    static cp_runtime_t runtime;
    controller_profile_meta_t meta = {0};
    cp_event_t event;
    assert(bytes == 6176u && cp_profile_parse(blob, bytes, &profile) == CP_OK);
    assert(controller_profile_meta_parse(blob, bytes, &meta) == ESP_OK && meta.valid);
    assert(profile.vid == 0x2b73u && profile.pid == 0x0026u);
    assert(profile.input_count == 288u && profile.output_count == 127u);
    cp_runtime_init(&runtime);
    for (unsigned deck = 0u; deck < 2u; ++deck) {
        for (unsigned command=0; command<2; ++command) {
            assert(cp_runtime_process(&profile,&runtime,0x90u+deck,0x3du+command,127,&event));
            assert(event.id==(deck ? CTRL_ID_DECK2_EXT_ACTION : CTRL_ID_DECK1_EXT_ACTION));
            unsigned action=command ? CTRL_DECK_EXT_ACTION_MEMORY_DELETE : CTRL_DECK_EXT_ACTION_MEMORY_STORE;
            assert(event.value==CTRL_DECK_EXT_VALUE(action,true));
            assert(cp_runtime_process(&profile,&runtime,0x90u+deck,0x3du+command,0,&event));
            assert(event.value==CTRL_DECK_EXT_VALUE(action,false));
        }
        const uint8_t ids[] = {
            deck ? CTRL_ID_DECK2_LOOP_HALVE : CTRL_ID_DECK1_LOOP_HALVE,
            deck ? CTRL_ID_DECK2_LOOP_DOUBLE : CTRL_ID_DECK1_LOOP_DOUBLE,
        };
        for (unsigned command = 0u; command < 2u; ++command) {
            assert(cp_runtime_process(&profile, &runtime, 0x90u + deck,
                0x51u + 2u * command, 127u, &event));
            assert(event.id == ids[command] && event.value == 1);
            assert(cp_runtime_process(&profile, &runtime, 0x90u + deck,
                0x51u + 2u * command, 0u, &event));
            assert(event.id == ids[command] && event.value == 0);
        }
    }
    blob[bytes - 1u] ^= 1u;
    assert(cp_profile_parse(blob, bytes, &profile) == CP_ERR_CRC);
    assert(controller_profile_meta_parse(blob, bytes, &meta) != ESP_OK);
    puts("DDJ-400 v4: parser/storage CRC and both-deck loop press/release PASS");
    return 0;
}
