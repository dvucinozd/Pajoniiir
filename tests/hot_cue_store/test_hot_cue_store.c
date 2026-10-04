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
    blob.override_mask = 0x07; /* slot 1 is a persistent deletion */
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
    assert(loaded.override_mask == 0x07);
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
    hot_cue_store_blob_t blob = {.valid_mask = 1u, .override_mask = 1u};
    blob.slots[0].pos_ms = 500u;
    blob.slots[0].type = HOT_CUE_STORE_TYPE_SINGLE;
    assert(hot_cue_store_save(&first, &blob) == ESP_OK);
    assert(hot_cue_store_load(&collision, &blob) == ESP_ERR_INVALID_STATE);
    assert(hot_cue_store_save(&collision, &blob) == ESP_ERR_INVALID_STATE);
    assert(hot_cue_store_clear(&collision) == ESP_ERR_INVALID_STATE);
    assert(hot_cue_store_clear(&first) == ESP_OK);
}

static void test_source_merge_and_persistent_deletion(void)
{
    hot_cue_store_blob_t source={.valid_mask=3u};
    source.slots[0]=(hot_cue_store_slot_t){.pos_ms=1000u,.type=HOT_CUE_STORE_TYPE_SINGLE};
    source.slots[1]=(hot_cue_store_slot_t){.pos_ms=2000u,.type=HOT_CUE_STORE_TYPE_SINGLE};
    hot_cue_store_blob_t local={.valid_mask=4u,.override_mask=7u};
    local.slots[2]=(hot_cue_store_slot_t){.pos_ms=3000u,.type=HOT_CUE_STORE_TYPE_SINGLE};
    hot_cue_store_blob_t effective={0};
    hot_cue_store_merge(&source,&local,&effective);
    assert(effective.valid_mask==4u);
    assert(effective.slots[0].pos_ms==0u);
    assert(effective.slots[2].pos_ms==3000u);
    hot_cue_store_merge(&source,NULL,&effective);
    assert(effective.valid_mask==3u);

    media_persistent_id_t id=identity(0x60);
    assert(hot_cue_store_save(&id,&local)==ESP_OK);
    hot_cue_store_blob_t loaded={0};
    assert(hot_cue_store_load(&id,&loaded)==ESP_OK);
    hot_cue_store_merge(&source,&loaded,&effective);
    assert(effective.valid_mask==4u);
    assert(hot_cue_store_reset_to_source(&id)==ESP_OK);
    assert(hot_cue_store_load(&id,&loaded)==ESP_OK);
    hot_cue_store_merge(&source,&loaded,&effective);
    assert(effective.valid_mask==3u);
}

static void test_v2_slots_are_authoritative_until_explicit_reset(void)
{
    media_persistent_id_t id=identity(0x80);
    hot_cue_store_blob_t legacy={.valid_mask=1u};
    legacy.slots[0]=(hot_cue_store_slot_t){.pos_ms=500u,.type=HOT_CUE_STORE_TYPE_SINGLE};
    assert(hot_cue_store_test_seed_v2(&id,&legacy)==ESP_OK);
    hot_cue_store_blob_t loaded={0};
    assert(hot_cue_store_load(&id,&loaded)==ESP_OK);
    assert(loaded.version==2u);
    assert(loaded.override_mask==0xffu);
    hot_cue_store_blob_t source={.valid_mask=3u};
    source.slots[0]=(hot_cue_store_slot_t){.pos_ms=1000u,.type=HOT_CUE_STORE_TYPE_SINGLE};
    source.slots[1]=(hot_cue_store_slot_t){.pos_ms=2000u,.type=HOT_CUE_STORE_TYPE_SINGLE};
    hot_cue_store_blob_t effective={0};
    hot_cue_store_merge(&source,&loaded,&effective);
    assert(effective.valid_mask==1u && effective.slots[0].pos_ms==500u);
    assert(hot_cue_store_reset_to_source(&id)==ESP_OK);
    assert(hot_cue_store_load(&id,&loaded)==ESP_OK);
    assert(loaded.version==3u && loaded.override_mask==0u);
    hot_cue_store_merge(&source,&loaded,&effective);
    assert(effective.valid_mask==3u && effective.slots[0].pos_ms==1000u);
}

int main(void)
{
    test_save_load_clear_roundtrip();
    test_rejects_invalid_track_key();
    test_short_key_collision_is_fail_closed();
    test_source_merge_and_persistent_deletion();
    test_v2_slots_are_authoritative_until_explicit_reset();
    puts("hot_cue_store tests passed");
    return 0;
}
