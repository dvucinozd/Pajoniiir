#include "hot_cue_store.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static media_persistent_id_t identity(uint8_t seed)
{
    media_persistent_id_t id = {.valid = true};
    for (size_t i = 0; i < sizeof(id.bytes); ++i) id.bytes[i] = (uint8_t)(seed + i);
    return id;
}

static void test_save_load_clear_roundtrip(void)
{
    hot_cue_store_blob_t blob = {0};
    blob.valid_mask = 0x05;
    blob.slots[0].pos_ms = 1000;
    blob.slots[0].type = HOT_CUE_STORE_TYPE_SINGLE;
    blob.slots[2].pos_ms = 2000;
    blob.slots[2].end_ms = 4000;
    blob.slots[2].type = HOT_CUE_STORE_TYPE_LOOP;

    media_persistent_id_t id = identity(0x20);
    assert(hot_cue_store_save(&id, &blob) == ESP_OK);

    hot_cue_store_blob_t loaded;
    memset(&loaded, 0xAA, sizeof(loaded));
    assert(hot_cue_store_load(&id, &loaded) == ESP_OK);
    assert(loaded.valid_mask == 0x05);
    assert(loaded.slots[0].pos_ms == 1000);
    assert(loaded.slots[2].end_ms == 4000);
    assert(loaded.slots[2].type == HOT_CUE_STORE_TYPE_LOOP);

    assert(hot_cue_store_clear(&id) == ESP_OK);
    assert(hot_cue_store_load(&id, &loaded) == ESP_ERR_NOT_FOUND);
}

static void test_rejects_invalid_track_key(void)
{
    hot_cue_store_blob_t blob = {0};
    media_persistent_id_t invalid = {0};
    assert(hot_cue_store_save(&invalid, &blob) == ESP_ERR_INVALID_ARG);
    assert(hot_cue_store_load(&invalid, &blob) == ESP_ERR_INVALID_ARG);
    assert(hot_cue_store_clear(&invalid) == ESP_ERR_INVALID_ARG);
}

static void test_short_key_collision_is_fail_closed(void)
{
    media_persistent_id_t first = identity(0x40);
    media_persistent_id_t collision = first;
    collision.bytes[31] ^= 0xffu;
    hot_cue_store_blob_t blob = {.valid_mask = 1u};
    blob.slots[0].pos_ms = 500u;
    blob.slots[0].type = HOT_CUE_STORE_TYPE_SINGLE;
    assert(hot_cue_store_save(&first, &blob) == ESP_OK);
    assert(hot_cue_store_load(&collision, &blob) == ESP_ERR_INVALID_STATE);
    assert(hot_cue_store_save(&collision, &blob) == ESP_ERR_INVALID_STATE);
    assert(hot_cue_store_clear(&collision) == ESP_ERR_INVALID_STATE);
    assert(hot_cue_store_clear(&first) == ESP_OK);
}

int main(void)
{
    test_save_load_clear_roundtrip();
    test_rejects_invalid_track_key();
    test_short_key_collision_is_fail_closed();
    puts("hot_cue_store tests passed");
    return 0;
}
