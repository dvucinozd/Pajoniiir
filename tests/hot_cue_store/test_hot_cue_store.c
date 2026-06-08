#include "hot_cue_store.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_save_load_clear_roundtrip(void)
{
    hot_cue_store_blob_t blob = {0};
    blob.valid_mask = 0x05;
    blob.slots[0].pos_ms = 1000;
    blob.slots[0].type = HOT_CUE_STORE_TYPE_SINGLE;
    blob.slots[2].pos_ms = 2000;
    blob.slots[2].end_ms = 4000;
    blob.slots[2].type = HOT_CUE_STORE_TYPE_LOOP;

    assert(hot_cue_store_save(1234, &blob) == ESP_OK);

    hot_cue_store_blob_t loaded;
    memset(&loaded, 0xAA, sizeof(loaded));
    assert(hot_cue_store_load(1234, &loaded) == ESP_OK);
    assert(loaded.valid_mask == 0x05);
    assert(loaded.slots[0].pos_ms == 1000);
    assert(loaded.slots[2].end_ms == 4000);
    assert(loaded.slots[2].type == HOT_CUE_STORE_TYPE_LOOP);

    assert(hot_cue_store_clear(1234) == ESP_OK);
    assert(hot_cue_store_load(1234, &loaded) == ESP_ERR_NOT_FOUND);
}

static void test_rejects_invalid_track_key(void)
{
    hot_cue_store_blob_t blob = {0};
    assert(hot_cue_store_save(0, &blob) == ESP_ERR_INVALID_ARG);
    assert(hot_cue_store_load(0, &blob) == ESP_ERR_INVALID_ARG);
    assert(hot_cue_store_clear(0) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    test_save_load_clear_roundtrip();
    test_rejects_invalid_track_key();
    puts("hot_cue_store tests passed");
    return 0;
}
