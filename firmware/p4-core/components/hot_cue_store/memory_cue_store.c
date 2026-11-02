#include "memory_cue_store.h"
#include <string.h>

#define RECORD_SIZE (40u + MEMORY_CUE_STORE_CAPACITY * 12u + 4u)
#define RECORD_MAGIC 0x31434d50u

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8u));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (unsigned j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static bool cue_valid(const hot_cue_store_slot_t *cue)
{
    return cue && ((cue->type == HOT_CUE_STORE_TYPE_SINGLE && !cue->end_ms) ||
        (cue->type == HOT_CUE_STORE_TYPE_LOOP && cue->end_ms > cue->pos_ms));
}
static bool bank_valid(const memory_cue_bank_t *bank)
{
    if (!bank || bank->count > MEMORY_CUE_STORE_CAPACITY) return false;
    for (unsigned i = 0; i < bank->count; ++i)
        if (!cue_valid(&bank->cues[i]) || (i && bank->cues[i-1].pos_ms >= bank->cues[i].pos_ms)) return false;
    return true;
}
esp_err_t memory_cue_bank_put(memory_cue_bank_t *bank, hot_cue_store_slot_t cue)
{
    if (!bank_valid(bank) || !cue_valid(&cue)) return ESP_ERR_INVALID_ARG;
    memset(cue.reserved, 0, sizeof(cue.reserved));
    unsigned at = 0;
    while (at < bank->count && bank->cues[at].pos_ms < cue.pos_ms) ++at;
    if (at < bank->count && bank->cues[at].pos_ms == cue.pos_ms) {
        bank->cues[at] = cue;
        return ESP_OK;
    }
    if (bank->count == MEMORY_CUE_STORE_CAPACITY) return ESP_ERR_INVALID_SIZE;
    memmove(bank->cues + at + 1, bank->cues + at, (bank->count - at) * sizeof(cue));
    bank->cues[at] = cue;
    ++bank->count;
    return ESP_OK;
}
esp_err_t memory_cue_bank_delete(memory_cue_bank_t *bank, uint32_t pos_ms)
{
    if (!bank_valid(bank)) return ESP_ERR_INVALID_ARG;
    for (unsigned i = 0; i < bank->count; ++i) {
        if (bank->cues[i].pos_ms != pos_ms) continue;
        memmove(bank->cues + i, bank->cues + i + 1, (bank->count - i - 1u) * sizeof(bank->cues[0]));
        memset(&bank->cues[--bank->count], 0, sizeof(bank->cues[0]));
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}
static bool key_for(const media_persistent_id_t *id, char key[16])
{
    static const char hex[] = "0123456789abcdef";
    if (!id || !id->valid) return false;
    key[0] = 'm';
    for (unsigned i = 0; i < 7; ++i) {
        key[1+i*2] = hex[id->bytes[i] >> 4];
        key[2+i*2] = hex[id->bytes[i] & 15];
    }
    key[15] = 0;
    return true;
}
static void encode(const media_persistent_id_t *id, const memory_cue_bank_t *bank, uint8_t *p)
{
    memset(p, 0, RECORD_SIZE);
    put32(p, RECORD_MAGIC);
    p[4] = 1; p[5] = bank->count;
    memcpy(p + 8, id->bytes, sizeof(id->bytes));
    for (unsigned i = 0; i < bank->count; ++i) {
        uint8_t *slot = p + 40 + i * 12;
        put32(slot, bank->cues[i].pos_ms);
        put32(slot + 4, bank->cues[i].end_ms);
        slot[8] = bank->cues[i].type;
    }
    put32(p + RECORD_SIZE - 4, crc32(p, RECORD_SIZE - 4));
}
static esp_err_t decode(const media_persistent_id_t *id, const uint8_t *p, size_t n, memory_cue_bank_t *out)
{
    if (n != RECORD_SIZE || get32(p) != RECORD_MAGIC || p[4] != 1 || p[6] || p[7]) return ESP_ERR_INVALID_SIZE;
    if (memcmp(p + 8, id->bytes, sizeof(id->bytes))) return ESP_ERR_INVALID_STATE;
    if (get32(p + n - 4) != crc32(p, n - 4)) return ESP_ERR_INVALID_CRC;
    memory_cue_bank_t bank = {.count = p[5]};
    if (bank.count > MEMORY_CUE_STORE_CAPACITY) return ESP_ERR_INVALID_SIZE;
    for (unsigned i = 0; i < MEMORY_CUE_STORE_CAPACITY; ++i) {
        const uint8_t *slot = p + 40 + i * 12;
        if (slot[9] || slot[10] || slot[11]) return ESP_ERR_INVALID_ARG;
        if (i >= bank.count) {
            if (get32(slot) || get32(slot + 4) || slot[8]) return ESP_ERR_INVALID_ARG;
        } else bank.cues[i] = (hot_cue_store_slot_t){.pos_ms=get32(slot), .end_ms=get32(slot+4), .type=slot[8]};
    }
    if (!bank_valid(&bank)) return ESP_ERR_INVALID_ARG;
    *out = bank;
    return ESP_OK;
}

#ifdef MEMORY_CUE_STORE_STANDALONE_TEST
/* Bounded backend for actor tests; the separate NVS harness executes the real
 * firmware branch below, including read/write/commit failures and collisions. */
static struct { bool used; char key[16]; uint8_t bytes[RECORD_SIZE]; } records[16];
static int find_record(const char *key)
{
    for (unsigned i = 0; i < 16; ++i) if (records[i].used && !strcmp(records[i].key, key)) return (int)i;
    return -1;
}
esp_err_t memory_cue_store_load(const media_persistent_id_t *id, memory_cue_bank_t *out)
{
    char key[16];
    if (!key_for(id, key) || !out) return ESP_ERR_INVALID_ARG;
    int at = find_record(key);
    return at < 0 ? ESP_ERR_NOT_FOUND : decode(id, records[at].bytes, RECORD_SIZE, out);
}
esp_err_t memory_cue_store_save(const media_persistent_id_t *id, const memory_cue_bank_t *bank)
{
    char key[16];
    if (!key_for(id, key) || !bank_valid(bank)) return ESP_ERR_INVALID_ARG;
    int at = find_record(key);
    if (at >= 0 && memcmp(records[at].bytes + 8, id->bytes, sizeof(id->bytes))) return ESP_ERR_INVALID_STATE;
    if (at < 0) for (unsigned i = 0; i < 16; ++i) if (!records[i].used) { at = (int)i; break; }
    if (at < 0) return ESP_ERR_NO_MEM;
    records[at].used = true; strcpy(records[at].key, key);
    encode(id, bank, records[at].bytes);
    return ESP_OK;
}
esp_err_t memory_cue_store_restore(const media_persistent_id_t *id)
{
    char key[16]; memory_cue_bank_t bank;
    if (!key_for(id, key)) return ESP_ERR_INVALID_ARG;
    esp_err_t rc = memory_cue_store_load(id, &bank);
    if (rc == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (rc != ESP_OK) return rc;
    memset(&records[find_record(key)], 0, sizeof(records[0]));
    return ESP_OK;
}
#else
#include "nvs.h"
static const char *NS = "memorycue_v1";
esp_err_t memory_cue_store_load(const media_persistent_id_t *id, memory_cue_bank_t *out)
{
    char key[16];
    if (!key_for(id, key) || !out) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t rc = nvs_open(NS, NVS_READONLY, &h);
    if (rc != ESP_OK) return rc == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : rc;
    uint8_t bytes[RECORD_SIZE]; size_t n = sizeof(bytes);
    rc = nvs_get_blob(h, key, bytes, &n); nvs_close(h);
    if (rc != ESP_OK) return rc == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : rc;
    return decode(id, bytes, n, out);
}
static esp_err_t change_record(const media_persistent_id_t *id, const memory_cue_bank_t *bank)
{
    char key[16]; memory_cue_bank_t previous;
    if (!key_for(id, key) || (bank && !bank_valid(bank))) return ESP_ERR_INVALID_ARG;
    esp_err_t rc = memory_cue_store_load(id, &previous);
    if (!bank && (rc == ESP_ERR_INVALID_CRC || rc == ESP_ERR_INVALID_ARG || rc == ESP_ERR_INVALID_SIZE)) {
        /* Explicit restore can recover a corrupt payload only if its complete
         * identity is still readable. Never erase a collision or truncated ID. */
        nvs_handle_t owner;
        rc = nvs_open(NS, NVS_READONLY, &owner);
        if (rc != ESP_OK) return rc;
        uint8_t bytes[RECORD_SIZE]; size_t n = sizeof(bytes);
        rc = nvs_get_blob(owner, key, bytes, &n); nvs_close(owner);
        if (rc != ESP_OK) return rc;
        if (n < 40u) return ESP_ERR_INVALID_SIZE;
        if (memcmp(bytes + 8, id->bytes, sizeof(id->bytes))) return ESP_ERR_INVALID_STATE;
    }
    if (rc != ESP_OK && rc != ESP_ERR_NOT_FOUND) return rc;
    if (!bank && rc == ESP_ERR_NOT_FOUND) return ESP_OK;
    nvs_handle_t h;
    rc = nvs_open(NS, NVS_READWRITE, &h);
    if (rc != ESP_OK) return rc;
    if (bank) {
        uint8_t bytes[RECORD_SIZE]; encode(id, bank, bytes);
        rc = nvs_set_blob(h, key, bytes, sizeof(bytes));
    } else rc = nvs_erase_key(h, key);
    if (rc == ESP_OK) rc = nvs_commit(h);
    nvs_close(h);
    return rc;
}
esp_err_t memory_cue_store_save(const media_persistent_id_t *id, const memory_cue_bank_t *bank)
{
    return bank ? change_record(id, bank) : ESP_ERR_INVALID_ARG;
}
esp_err_t memory_cue_store_restore(const media_persistent_id_t *id)
{
    return change_record(id, NULL);
}
#endif
