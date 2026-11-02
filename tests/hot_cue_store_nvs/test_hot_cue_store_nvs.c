/* Execute the production NVS branch, without HOT_CUE_STORE_STANDALONE_TEST.
 * This bounded fake models API errors and stored blobs, not flash atomicity.
 * Writes may already be visible when commit fails: never infer rollback from
 * a failed commit. Real power-loss/reboot acceptance still requires hardware. */
#include "hot_cue_store.h"
#include "memory_cue_store.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { bool used; char ns[16], key[16]; uint8_t data[256]; size_t len; } entry_t;
typedef struct { bool used; char ns[16]; nvs_open_mode_t mode; } handle_t;
static entry_t entries[16];
static handle_t handles[8];
static bool fail_open_rw, fail_read, fail_set, fail_commit, fail_erase;
static unsigned sets, commits;

static handle_t *handle(nvs_handle_t h)
{
    assert(h > 0 && h <= 8 && handles[h-1].used);
    return &handles[h-1];
}
static entry_t *find(const char *ns, const char *key)
{
    for (unsigned i=0; i<16; ++i)
        if (entries[i].used && !strcmp(entries[i].ns,ns) && !strcmp(entries[i].key,key)) return &entries[i];
    return NULL;
}
static void closed(void)
{
    for (unsigned i=0; i<8; ++i) assert(!handles[i].used);
}
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out)
{
    if (mode == NVS_READWRITE && fail_open_rw) return ESP_FAIL;
    bool exists=false;
    for (unsigned i=0; i<16; ++i) if (entries[i].used && !strcmp(entries[i].ns,ns)) exists=true;
    if (!exists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    for (unsigned i=0; i<8; ++i) if (!handles[i].used) {
        handles[i].used=true; handles[i].mode=mode;
        assert(strlen(ns)<sizeof(handles[i].ns)); strcpy(handles[i].ns,ns);
        *out=i+1; return ESP_OK;
    }
    assert(!"handle leak"); return ESP_FAIL;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *data, size_t *len)
{
    handle_t *v=handle(h);
    if (fail_read) return ESP_FAIL;
    entry_t *e=find(v->ns,key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (*len<e->len) return ESP_ERR_INVALID_SIZE;
    memcpy(data,e->data,e->len); *len=e->len; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len)
{
    handle_t *v=handle(h); assert(v->mode==NVS_READWRITE); sets++;
    if (fail_set) return ESP_FAIL;
    entry_t *e=find(v->ns,key);
    if (!e) for (unsigned i=0; i<16; ++i) if (!entries[i].used) { e=&entries[i]; break; }
    assert(e && len<=sizeof(e->data) && strlen(key)<sizeof(e->key));
    e->used=true; strcpy(e->ns,v->ns); strcpy(e->key,key);
    memcpy(e->data,data,len); e->len=len; return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    handle_t *v=handle(h); assert(v->mode==NVS_READWRITE);
    if (fail_erase) return ESP_FAIL;
    entry_t *e=find(v->ns,key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    memset(e,0,sizeof(*e)); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    assert(handle(h)->mode==NVS_READWRITE); commits++;
    return fail_commit?ESP_FAIL:ESP_OK;
}
void nvs_close(nvs_handle_t h) { memset(handle(h),0,sizeof(handle_t)); }

static media_persistent_id_t identity(const char *export_name)
{
    uint8_t digest[32]; media_persistent_id_t id={0};
    media_sha256(export_name,strlen(export_name),digest);
    assert(media_persistent_id_derive(digest,"/Contents/same.wav",4096,123456,&id));
    return id;
}
static hot_cue_store_blob_t point(uint32_t ms)
{
    hot_cue_store_blob_t b={.valid_mask=1,.override_mask=1};
    b.slots[0]=(hot_cue_store_slot_t){.pos_ms=ms,.type=HOT_CUE_STORE_TYPE_SINGLE};
    return b;
}
static void load_point(const media_persistent_id_t *id, uint32_t ms)
{
    hot_cue_store_blob_t b={0};
    assert(hot_cue_store_load(id,&b)==ESP_OK && b.slots[0].pos_ms==ms); closed();
}
static void test_export_isolation_tombstone_and_restore(void)
{
    /* Same path/stat and catalog track number can belong to different exports.
     * Numeric track ID is intentionally absent from the persistent identity. */
    media_persistent_id_t a=identity("export-A"), b=identity("export-B");
    assert(!media_persistent_id_equal(&a,&b));
    hot_cue_store_blob_t source=point(1000), local=point(5000), other=point(9000), loaded={0}, merged={0};
    source.valid_mask=3; source.slots[1]=source.slots[0]; source.slots[1].pos_ms=2000;
    local.override_mask=3; /* Explicit deletion of source slot B. */
    assert(hot_cue_store_save(&a,&local)==ESP_OK); closed();
    assert(hot_cue_store_save(&b,&other)==ESP_OK); closed();
    memset(handles,0,sizeof(handles)); /* Reopen against retained fake storage. */
    assert(hot_cue_store_load(&a,&loaded)==ESP_OK); closed();
    hot_cue_store_merge(&source,&loaded,&merged);
    assert(merged.valid_mask==1 && merged.slots[0].pos_ms==5000 && merged.slots[1].pos_ms==0);
    load_point(&b,9000);
    assert(hot_cue_store_reset_to_source(&a)==ESP_OK); closed();
    assert(hot_cue_store_load(&a,&loaded)==ESP_OK && loaded.override_mask==0); closed();
    hot_cue_store_merge(&source,&loaded,&merged);
    assert(merged.valid_mask==3 && merged.slots[0].pos_ms==1000);
    load_point(&b,9000);
    assert(hot_cue_store_clear(&a)==ESP_OK); closed();
    assert(hot_cue_store_load(&a,&loaded)==ESP_ERR_NOT_FOUND); closed();
    load_point(&b,9000);
}
static void test_api_failures_are_reported_and_handles_closed(void)
{
    media_persistent_id_t id=identity("errors"); hot_cue_store_blob_t old=point(3000), next=point(6000), loaded={0};
    assert(hot_cue_store_save(&id,&old)==ESP_OK); closed();
    unsigned before=commits;
    fail_open_rw=true; assert(hot_cue_store_save(&id,&next)==ESP_FAIL); fail_open_rw=false; closed();
    assert(commits==before); load_point(&id,3000);
    fail_read=true; assert(hot_cue_store_load(&id,&loaded)==ESP_FAIL);
    unsigned writes=sets; assert(hot_cue_store_save(&id,&next)==ESP_FAIL); assert(sets==writes);
    fail_read=false; closed();
    fail_set=true; assert(hot_cue_store_save(&id,&next)==ESP_FAIL); fail_set=false; closed();
    assert(commits==before); load_point(&id,3000);
    fail_commit=true; assert(hot_cue_store_save(&id,&next)==ESP_FAIL); fail_commit=false; closed();
    assert(commits==before+1); /* No promise of old contents after commit failure. */
    fail_erase=true; before=commits; assert(hot_cue_store_clear(&id)==ESP_FAIL); fail_erase=false; closed();
    assert(commits==before);
    assert(hot_cue_store_save(&id,&old)==ESP_OK); closed();
    fail_commit=true; assert(hot_cue_store_reset_to_source(&id)==ESP_FAIL); fail_commit=false; closed();
}
static void test_truncated_or_corrupt_record_does_not_publish_or_get_overwritten(void)
{
    media_persistent_id_t id=identity("damaged"); hot_cue_store_blob_t local=point(7000), sentinel, out;
    assert(hot_cue_store_save(&id,&local)==ESP_OK); closed();
    entry_t *e=NULL;
    for (unsigned i=0; i<16; ++i) if (entries[i].used && !memcmp(entries[i].data+8,id.bytes,32)) e=&entries[i];
    assert(e); entry_t intact=*e;
    memset(&sentinel,0x5a,sizeof(sentinel)); out=sentinel;
    e->len--; assert(hot_cue_store_load(&id,&out)==ESP_ERR_INVALID_SIZE); closed();
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    assert(hot_cue_store_save(&id,&local)==ESP_OK); closed(); load_point(&id,7000);
    *e=intact; e->data[e->len-1]^=1;
    assert(hot_cue_store_load(&id,&out)==ESP_ERR_INVALID_CRC); closed();
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    assert(hot_cue_store_save(&id,&local)==ESP_OK); closed(); load_point(&id,7000);
    e->data[e->len-1]^=1;
    assert(hot_cue_store_clear(&id)==ESP_OK); closed();
    assert(hot_cue_store_load(&id,&out)==ESP_ERR_NOT_FOUND); closed();
    *e=intact; e->len=39u;
    unsigned writes=sets;
    assert(hot_cue_store_save(&id,&local)==ESP_ERR_INVALID_SIZE && sets==writes); closed();
    assert(hot_cue_store_clear(&id)==ESP_ERR_INVALID_SIZE); closed();
    *e=intact; load_point(&id,7000);
}
static void test_clear_removes_legacy_before_current(void)
{
    media_persistent_id_t id=identity("legacy-clear");
    hot_cue_store_blob_t local=point(8000), out={0};
    assert(hot_cue_store_save(&id,&local)==ESP_OK); closed();
    entry_t *current=NULL, *legacy=NULL;
    for (unsigned i=0;i<16u;++i) {
        if (entries[i].used && !memcmp(entries[i].data+8,id.bytes,32u)) current=&entries[i];
        if (!entries[i].used) legacy=&entries[i];
    }
    assert(current && legacy);
    *legacy=*current; strcpy(legacy->ns,"hotcue_v2");
    /* A readable owned legacy payload must also be removed, even if corrupt. */
    assert(hot_cue_store_clear(&id)==ESP_OK); closed();
    assert(!current->used && !legacy->used);
    assert(hot_cue_store_load(&id,&out)==ESP_ERR_NOT_FOUND); closed();
}
static void test_full_identity_collision_fails_closed(void)
{
    media_persistent_id_t id=identity("collision"), other=id; other.bytes[31]^=1;
    hot_cue_store_blob_t local=point(4000), out={0};
    assert(hot_cue_store_save(&id,&local)==ESP_OK); closed();
    assert(hot_cue_store_load(&other,&out)==ESP_ERR_INVALID_STATE); closed();
    unsigned writes=sets;
    assert(hot_cue_store_save(&other,&local)==ESP_ERR_INVALID_STATE && sets==writes); closed();
    assert(hot_cue_store_clear(&other)==ESP_ERR_INVALID_STATE); closed();
    assert(hot_cue_store_reset_to_source(&other)==ESP_ERR_INVALID_STATE); closed();
    load_point(&id,4000);
}
static void test_memory_bank_persistence_capacity_identity_and_failures(void)
{
    memset(entries, 0, sizeof(entries));
    media_persistent_id_t id = identity("memory-a"), other = identity("memory-b");
    memory_cue_bank_t bank = {0}, out = {0};
    assert(memory_cue_store_load(&id, &out) == ESP_ERR_NOT_FOUND); closed();
    for (unsigned i = 16; i > 0; --i)
        assert(memory_cue_bank_put(&bank, (hot_cue_store_slot_t){.pos_ms=i*1000u,.type=1}) == ESP_OK);
    assert(bank.count == 16 && bank.cues[0].pos_ms == 1000 && bank.cues[15].pos_ms == 16000);
    assert(memory_cue_bank_put(&bank, (hot_cue_store_slot_t){.pos_ms=17000,.type=1}) == ESP_ERR_INVALID_SIZE);
    assert(memory_cue_bank_put(&bank, (hot_cue_store_slot_t){.pos_ms=5000,.end_ms=8000,.type=2}) == ESP_OK);
    assert(bank.count == 16 && bank.cues[4].end_ms == 8000);
    assert(memory_cue_bank_put(&bank, (hot_cue_store_slot_t){.pos_ms=5000,.end_ms=4000,.type=2}) == ESP_ERR_INVALID_ARG);
    assert(memory_cue_store_save(&id, &bank) == ESP_OK); closed();
    assert(memory_cue_store_load(&id, &out) == ESP_OK && !memcmp(&bank, &out, sizeof(bank))); closed();
    assert(memory_cue_store_load(&other, &out) == ESP_ERR_NOT_FOUND); closed();
    media_persistent_id_t collision = id; collision.bytes[31] ^= 1;
    unsigned writes = sets;
    assert(memory_cue_store_save(&collision, &bank) == ESP_ERR_INVALID_STATE && sets == writes); closed();
    assert(memory_cue_store_restore(&collision) == ESP_ERR_INVALID_STATE); closed();
    fail_read = true;
    assert(memory_cue_store_save(&id, &bank) == ESP_FAIL && sets == writes); fail_read=false; closed();
    fail_open_rw=true;
    assert(memory_cue_store_save(&id, &bank) == ESP_FAIL); fail_open_rw=false; closed();
    fail_set=true;
    assert(memory_cue_store_save(&id, &bank) == ESP_FAIL); fail_set=false; closed();
    fail_commit=true;
    assert(memory_cue_store_save(&id, &bank) == ESP_FAIL); fail_commit=false; closed();
    fail_erase=true;
    assert(memory_cue_store_restore(&id) == ESP_FAIL); fail_erase=false; closed();
    entry_t *record = NULL;
    for (unsigned i=0; i<16; ++i) if (entries[i].used && !strcmp(entries[i].ns,"memorycue_v1")) record=&entries[i];
    assert(record); entry_t intact=*record;
    record->data[record->len-1] ^= 1;
    memset(&out, 0x5a, sizeof(out)); memory_cue_bank_t sentinel=out;
    assert(memory_cue_store_load(&id, &out) == ESP_ERR_INVALID_CRC && !memcmp(&out,&sentinel,sizeof(out))); closed();
    writes=sets;
    assert(memory_cue_store_save(&id, &bank) == ESP_ERR_INVALID_CRC && sets==writes); closed();
    assert(memory_cue_store_restore(&id)==ESP_OK); closed();
    *record=intact; --record->len;
    assert(memory_cue_store_load(&id, &out) == ESP_ERR_INVALID_SIZE); closed();
    record->len=39u;
    assert(memory_cue_store_restore(&id)==ESP_ERR_INVALID_SIZE); closed();
    *record=intact;
    for (unsigned i=1; i<=16; ++i) assert(memory_cue_bank_delete(&bank,i*1000u)==ESP_OK);
    assert(memory_cue_bank_delete(&bank,1000)==ESP_ERR_NOT_FOUND);
    assert(memory_cue_store_save(&id,&bank)==ESP_OK); closed();
    assert(memory_cue_store_load(&id,&out)==ESP_OK && out.count==0); closed();
    assert(memory_cue_store_restore(&id)==ESP_OK); closed();
    assert(memory_cue_store_load(&id,&out)==ESP_ERR_NOT_FOUND); closed();
    assert(memory_cue_store_restore(&id)==ESP_OK); closed();
}

int main(void)
{
    test_export_isolation_tombstone_and_restore();
    test_api_failures_are_reported_and_handles_closed();
    test_truncated_or_corrupt_record_does_not_publish_or_get_overwritten();
    test_full_identity_collision_fails_closed();
    test_clear_removes_legacy_before_current();
    test_memory_bank_persistence_capacity_identity_and_failures();
    puts("hot_cue_store_nvs: production NVS adapter contract tests passed");
    return 0;
}
