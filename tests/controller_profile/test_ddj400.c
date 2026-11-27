#include "controller_profile.h"
#include "controller_profile_manager.h"
#include "control_link.h"
#include <assert.h>
#include <stdio.h>

static ctrl_event_t queued_event;

static esp_err_t capture_event(const ctrl_event_t *event)
{
    queued_event = *event;
    return ESP_OK;
}

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
    assert(control_link_init(capture_event) == ESP_OK);
    cp_runtime_init(&runtime);
    for (unsigned deck = 0u; deck < 2u; ++deck) {
        for (unsigned command=0; command<2; ++command) {
            assert(cp_runtime_process(&profile,&runtime,0x90u+deck,0x3du+command,127,&event));
            assert(event.id==(deck ? CTRL_ID_DECK2_EXT_ACTION : CTRL_ID_DECK1_EXT_ACTION));
            unsigned action=command ? CTRL_DECK_EXT_ACTION_MEMORY_DELETE : CTRL_DECK_EXT_ACTION_MEMORY_STORE;
            assert(event.value==CTRL_DECK_EXT_VALUE(action,true));
            queued_event.expected_track_generation = UINT32_MAX;
            assert(control_link_inject_semantic(event.type, event.id, event.value) == ESP_OK);
            assert(queued_event.type == CTRL_EV_BUTTON && queued_event.deck == deck);
            assert(queued_event.control == CTRL_DECK_CTL_EXT_ACTION &&
                   queued_event.id == event.id && queued_event.value == event.value);
            assert(queued_event.expected_track_generation == 0u);
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

    /* Recreate the four bindings from the earlier JC1060 binary profile. */
    for (unsigned i = 0; i < profile.input_count; ++i) {
        uint8_t *entry = blob + CP_HEADER_SIZE + i * CP_INPUT_ENTRY_SIZE;
        if ((entry[0] == 0x90 || entry[0] == 0x91) &&
            (entry[1] == 0x3d || entry[1] == 0x3e) &&
            entry[8] == CTRL_DECK_EXT_ACTION_MEMORY_STORE + entry[1] - 0x3d) {
            entry[8] = 10 + entry[1] - 0x3d;
        }
    }
    uint32_t crc = cp_crc32(blob + 16, bytes - 16);
    assert(crc == 0x230e3f3cu);
    for (unsigned i = 0; i < 4; ++i) blob[12 + i] = (uint8_t)(crc >> (8 * i));
    assert(cp_profile_parse(blob, bytes, &profile) == CP_OK);
    cp_runtime_init(&runtime);
    for (unsigned deck = 0; deck < 2; ++deck) {
        assert(cp_runtime_process(&profile, &runtime, 0x90 + deck, 0x3d, 127, &event));
        assert(event.value == CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_MEMORY_STORE, true));
        assert(cp_runtime_process(&profile, &runtime, 0x90 + deck, 0x3e, 127, &event));
        assert(event.value == CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_MEMORY_DELETE, true));
    }
    /* Changing another payload field makes this a custom profile. The same
     * status/note and action 10 must then retain the canonical JOG_TOGGLE. */
    blob[CP_HEADER_SIZE + CP_INPUT_ENTRY_SIZE - 1u] ^= 1u;
    crc = cp_crc32(blob + 16, bytes - 16);
    for (unsigned i = 0; i < 4; ++i) blob[12 + i] = (uint8_t)(crc >> (8 * i));
    assert(cp_profile_parse(blob, bytes, &profile) == CP_OK);
    cp_runtime_init(&runtime);
    assert(cp_runtime_process(&profile, &runtime, 0x90, 0x3d, 127, &event));
    assert(event.value == CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_JOG_TOGGLE, true));
    blob[bytes - 1u] ^= 1u;
    assert(cp_profile_parse(blob, bytes, &profile) == CP_ERR_CRC);
    assert(controller_profile_meta_parse(blob, bytes, &meta) != ESP_OK);
    puts("DDJ-400 v4: parser/storage CRC and both-deck loop press/release PASS");
    return 0;
}
