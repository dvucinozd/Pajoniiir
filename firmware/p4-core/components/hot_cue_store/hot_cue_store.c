#include "hot_cue_store.h"

#include <stdio.h>
#include <string.h>

#define HOT_CUE_STORE_VERSION 3u
#define RECORD_MAGIC_V2 0x32435648u
#define RECORD_MAGIC_V3 0x33435648u
#define RECORD_V2_HEADER_SIZE 44u
#define RECORD_HEADER_SIZE 48u
#define RECORD_SLOT_SIZE 12u
#define RECORD_CRC_OFFSET (RECORD_HEADER_SIZE + HOT_CUE_STORE_SLOT_COUNT * RECORD_SLOT_SIZE)
#define RECORD_SIZE (RECORD_CRC_OFFSET + 4u)
#define RECORD_V2_SIZE (RECORD_V2_HEADER_SIZE + HOT_CUE_STORE_SLOT_COUNT * RECORD_SLOT_SIZE + 4u)

static void put_u16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put_u32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8u)); }
static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static uint32_t get_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static uint32_t crc32_iso(const uint8_t *data, size_t len)
{
    uint32_t crc=0xffffffffu;
    for (size_t i=0;i<len;i++) {
        crc^=data[i];
        for (unsigned bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
    }
    return ~crc;
}

static bool valid_id(const media_persistent_id_t *id) { return id && id->valid; }

static esp_err_t make_key(const media_persistent_id_t *id, char out[16])
{
    static const char hex[]="0123456789abcdef";
    if (!valid_id(id)||!out) return ESP_ERR_INVALID_ARG;
    out[0]='h';
    for (unsigned i=0;i<7;i++) { out[1u+i*2u]=hex[id->bytes[i]>>4]; out[2u+i*2u]=hex[id->bytes[i]&15u]; }
    out[15]='\0'; return ESP_OK;
}

static void normalize_blob(hot_cue_store_blob_t *blob)
{
    blob->version=HOT_CUE_STORE_VERSION; blob->valid_mask&=0xffu; blob->override_mask&=0xffu;
    blob->valid_mask&=blob->override_mask;
    for (uint32_t i=0;i<HOT_CUE_STORE_SLOT_COUNT;i++) {
        if ((blob->valid_mask&(1u<<i))==0u) memset(&blob->slots[i],0,sizeof(blob->slots[i]));
        else if (blob->slots[i].type==HOT_CUE_STORE_TYPE_LOOP) {
            if (blob->slots[i].end_ms<=blob->slots[i].pos_ms) {
                blob->valid_mask&=~(1u<<i); memset(&blob->slots[i],0,sizeof(blob->slots[i]));
            }
        } else { blob->slots[i].type=HOT_CUE_STORE_TYPE_SINGLE; blob->slots[i].end_ms=0u; }
    }
}

esp_err_t hot_cue_store_reset_to_source(const media_persistent_id_t *id)
{
    const hot_cue_store_blob_t empty={0};
    return hot_cue_store_save(id,&empty);
}

void hot_cue_store_merge(const hot_cue_store_blob_t *source,
                         const hot_cue_store_blob_t *local,
                         hot_cue_store_blob_t *out)
{
    if (!out) return;
    hot_cue_store_blob_t merged={0};
    merged.version=HOT_CUE_STORE_VERSION;
    for (unsigned i=0;i<HOT_CUE_STORE_SLOT_COUNT;i++) {
        const uint32_t bit=1u<<i;
        const hot_cue_store_blob_t *selected=
            local && (local->override_mask&bit) ? local : source;
        if (selected && (selected->valid_mask&bit)) {
            merged.valid_mask|=bit;
            merged.slots[i]=selected->slots[i];
        }
    }
    *out=merged;
}

static void encode_record(const media_persistent_id_t *id,const hot_cue_store_blob_t *source,uint8_t record[RECORD_SIZE])
{
    hot_cue_store_blob_t blob=*source; normalize_blob(&blob); memset(record,0,RECORD_SIZE);
    put_u32(record,RECORD_MAGIC_V3); put_u16(record+4,HOT_CUE_STORE_VERSION);
    put_u16(record+6,(uint16_t)(8u+HOT_CUE_STORE_SLOT_COUNT*RECORD_SLOT_SIZE));
    memcpy(record+8,id->bytes,32u); put_u32(record+40,blob.valid_mask);
    put_u32(record+44,blob.override_mask);
    for (unsigned i=0;i<HOT_CUE_STORE_SLOT_COUNT;i++) {
        uint8_t *slot=record+RECORD_HEADER_SIZE+i*RECORD_SLOT_SIZE;
        put_u32(slot,blob.slots[i].pos_ms); put_u32(slot+4,blob.slots[i].end_ms); slot[8]=blob.slots[i].type;
    }
    put_u32(record+RECORD_CRC_OFFSET,crc32_iso(record,RECORD_CRC_OFFSET));
}

static esp_err_t decode_record(const media_persistent_id_t *id,const uint8_t *record,size_t len,hot_cue_store_blob_t *out)
{
    if (len!=RECORD_SIZE && len!=RECORD_V2_SIZE) return ESP_ERR_INVALID_SIZE;
    const bool legacy=len==RECORD_V2_SIZE;
    if (get_u32(record)!=(legacy?RECORD_MAGIC_V2:RECORD_MAGIC_V3)||
        get_u16(record+4)!=(legacy?2u:HOT_CUE_STORE_VERSION)||
        get_u16(record+6)!=(legacy?4u:8u)+HOT_CUE_STORE_SLOT_COUNT*RECORD_SLOT_SIZE)
        return ESP_ERR_INVALID_SIZE;
    if (memcmp(record+8,id->bytes,32u)!=0) return ESP_ERR_INVALID_STATE;
    const size_t crc_offset=len-4u;
    if (get_u32(record+crc_offset)!=crc32_iso(record,crc_offset)) return ESP_ERR_INVALID_CRC;
    memset(out,0,sizeof(*out)); out->version=legacy?2u:HOT_CUE_STORE_VERSION;
    out->valid_mask=get_u32(record+40);
    out->override_mask=legacy?0xffu:get_u32(record+44);
    if (((out->valid_mask|out->override_mask)&~0xffu)!=0u ||
        (out->valid_mask&~out->override_mask)!=0u) return ESP_ERR_INVALID_ARG;
    for (unsigned i=0;i<HOT_CUE_STORE_SLOT_COUNT;i++) {
        const uint8_t *slot=record+(legacy?RECORD_V2_HEADER_SIZE:RECORD_HEADER_SIZE)+i*RECORD_SLOT_SIZE;
        out->slots[i].pos_ms=get_u32(slot); out->slots[i].end_ms=get_u32(slot+4); out->slots[i].type=slot[8];
        if ((out->valid_mask&(1u<<i))==0u) {
            if (out->slots[i].pos_ms||out->slots[i].end_ms||out->slots[i].type) return ESP_ERR_INVALID_ARG;
        } else if (out->slots[i].type==HOT_CUE_STORE_TYPE_SINGLE) {
            if (out->slots[i].end_ms!=0u) return ESP_ERR_INVALID_ARG;
        } else if (out->slots[i].type==HOT_CUE_STORE_TYPE_LOOP) {
            if (out->slots[i].end_ms<=out->slots[i].pos_ms) return ESP_ERR_INVALID_ARG;
        } else return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

#if defined(HOT_CUE_STORE_STANDALONE_TEST)
typedef struct { char key[16]; uint8_t record[RECORD_SIZE]; size_t len; int valid; int legacy; } test_entry_t;
static test_entry_t s_entries[16];
static test_entry_t *find_key(const char *key,int legacy)
{
    for (size_t i=0;i<sizeof(s_entries)/sizeof(s_entries[0]);i++)
        if (s_entries[i].valid&&s_entries[i].legacy==legacy&&strcmp(s_entries[i].key,key)==0) return &s_entries[i];
    return NULL;
}
/* Fixture-only writer for compatibility tests; firmware never creates v2. */
esp_err_t hot_cue_store_test_seed_v2(const media_persistent_id_t *id,const hot_cue_store_blob_t *blob)
{
    if (!valid_id(id)||!blob) return ESP_ERR_INVALID_ARG;
    char key[16]; make_key(id,key); test_entry_t *entry=find_key(key,1);
    if (!entry) for (size_t i=0;i<sizeof(s_entries)/sizeof(s_entries[0]);i++)
        if (!s_entries[i].valid) { entry=&s_entries[i]; break; }
    if (!entry) return ESP_FAIL;
    memset(entry,0,sizeof(*entry)); strcpy(entry->key,key);
    memcpy(entry->record+8,id->bytes,32u); put_u32(entry->record+40,blob->valid_mask&0xffu);
    for (unsigned i=0;i<HOT_CUE_STORE_SLOT_COUNT;i++) {
        uint8_t *slot=entry->record+RECORD_V2_HEADER_SIZE+i*RECORD_SLOT_SIZE;
        if ((blob->valid_mask&(1u<<i))!=0u) {
            put_u32(slot,blob->slots[i].pos_ms);
            put_u32(slot+4,blob->slots[i].end_ms);
            slot[8]=blob->slots[i].type;
        }
    }
    put_u32(entry->record,RECORD_MAGIC_V2); put_u16(entry->record+4,2u);
    put_u16(entry->record+6,(uint16_t)(4u+HOT_CUE_STORE_SLOT_COUNT*RECORD_SLOT_SIZE));
    put_u32(entry->record+RECORD_V2_SIZE-4u,crc32_iso(entry->record,RECORD_V2_SIZE-4u));
    entry->len=RECORD_V2_SIZE; entry->legacy=1; entry->valid=1;
    return ESP_OK;
}
esp_err_t hot_cue_store_load(const media_persistent_id_t *id,hot_cue_store_blob_t *out)
{
    if (!valid_id(id)||!out) return ESP_ERR_INVALID_ARG;
    char key[16]; make_key(id,key); test_entry_t *entry=find_key(key,0);
    if (!entry) entry=find_key(key,1);
    return entry?decode_record(id,entry->record,entry->len,out):ESP_ERR_NOT_FOUND;
}
esp_err_t hot_cue_store_save(const media_persistent_id_t *id,const hot_cue_store_blob_t *blob)
{
    if (!valid_id(id)||!blob) return ESP_ERR_INVALID_ARG;
    char key[16]; make_key(id,key); test_entry_t *entry=find_key(key,0);
    if (entry&&memcmp(entry->record+8,id->bytes,32u)!=0) return ESP_ERR_INVALID_STATE;
    test_entry_t *legacy=find_key(key,1);
    if (legacy&&memcmp(legacy->record+8,id->bytes,32u)!=0) return ESP_ERR_INVALID_STATE;
    if (!entry) for (size_t i=0;i<sizeof(s_entries)/sizeof(s_entries[0]);i++) if (!s_entries[i].valid) { entry=&s_entries[i]; break; }
    if (!entry) return ESP_FAIL;
    strcpy(entry->key,key); encode_record(id,blob,entry->record);
    entry->len=RECORD_SIZE; entry->legacy=0; entry->valid=1; return ESP_OK;
}
esp_err_t hot_cue_store_clear(const media_persistent_id_t *id)
{
    if (!valid_id(id)) return ESP_ERR_INVALID_ARG;
    char key[16]; make_key(id,key); test_entry_t *entry=find_key(key,0);
    if (!entry) entry=find_key(key,1);
    if (!entry) return ESP_OK;
    if (memcmp(entry->record+8,id->bytes,32u)!=0) return ESP_ERR_INVALID_STATE;
    memset(entry,0,sizeof(*entry)); return ESP_OK;
}
#else
#include "nvs.h"
#include "esp_check.h"
#include "esp_log.h"
static const char *TAG="hot_cue_store";
static const char *NS_V2="hotcue_v2";
static const char *NS_V3="hotcue_v3";
static esp_err_t read_record(nvs_handle_t h,const char *key,uint8_t record[RECORD_SIZE],size_t expected)
{
    size_t len=RECORD_SIZE; esp_err_t rc=nvs_get_blob(h,key,record,&len);
    if (rc==ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (rc!=ESP_OK) return rc;
    return len==expected?ESP_OK:ESP_ERR_INVALID_SIZE;
}
static esp_err_t load_namespace(const char *ns,const media_persistent_id_t *id,
                                const char *key,size_t expected,hot_cue_store_blob_t *out)
{
    nvs_handle_t h; esp_err_t rc=nvs_open(ns,NVS_READONLY,&h);
    if (rc!=ESP_OK) return rc==ESP_ERR_NVS_NOT_FOUND?ESP_ERR_NOT_FOUND:rc;
    uint8_t record[RECORD_SIZE]; rc=read_record(h,key,record,expected); nvs_close(h);
    return rc==ESP_OK?decode_record(id,record,expected,out):rc;
}
esp_err_t hot_cue_store_load(const media_persistent_id_t *id,hot_cue_store_blob_t *out)
{
    if (!valid_id(id)||!out) return ESP_ERR_INVALID_ARG;
    char key[16]; ESP_RETURN_ON_ERROR(make_key(id,key),TAG,"key");
    esp_err_t rc=load_namespace(NS_V3,id,key,RECORD_SIZE,out);
    return rc==ESP_ERR_NOT_FOUND?
        load_namespace(NS_V2,id,key,RECORD_V2_SIZE,out):rc;
}
esp_err_t hot_cue_store_save(const media_persistent_id_t *id,const hot_cue_store_blob_t *blob)
{
    if (!valid_id(id)||!blob) return ESP_ERR_INVALID_ARG;
    char key[16]; ESP_RETURN_ON_ERROR(make_key(id,key),TAG,"key");
    hot_cue_store_blob_t existing={0};
    esp_err_t rc=hot_cue_store_load(id,&existing);
    if (rc!=ESP_OK&&rc!=ESP_ERR_NOT_FOUND) return rc;
    nvs_handle_t h; ESP_RETURN_ON_ERROR(nvs_open(NS_V3,NVS_READWRITE,&h),TAG,"nvs_open");
    uint8_t old[RECORD_SIZE]; rc=read_record(h,key,old,RECORD_SIZE);
    if (rc==ESP_OK&&memcmp(old+8,id->bytes,32u)!=0) rc=ESP_ERR_INVALID_STATE;
    else if (rc==ESP_ERR_NOT_FOUND) rc=ESP_OK;
    if (rc==ESP_OK) { uint8_t record[RECORD_SIZE]; encode_record(id,blob,record); rc=nvs_set_blob(h,key,record,sizeof(record)); }
    if (rc==ESP_OK) rc=nvs_commit(h);
    nvs_close(h); return rc;
}
esp_err_t hot_cue_store_clear(const media_persistent_id_t *id)
{
    if (!valid_id(id)) return ESP_ERR_INVALID_ARG;
    char key[16]; ESP_RETURN_ON_ERROR(make_key(id,key),TAG,"key");
    hot_cue_store_blob_t existing={0};
    esp_err_t rc=hot_cue_store_load(id,&existing);
    if (rc!=ESP_OK&&rc!=ESP_ERR_NOT_FOUND) return rc;
    nvs_handle_t h; rc=nvs_open(NS_V3,NVS_READWRITE,&h);
    if (rc==ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (rc!=ESP_OK) return rc;
    uint8_t old[RECORD_SIZE]; rc=read_record(h,key,old,RECORD_SIZE);
    if (rc==ESP_ERR_NOT_FOUND) rc=ESP_OK;
    else if (rc==ESP_OK&&memcmp(old+8,id->bytes,32u)!=0) rc=ESP_ERR_INVALID_STATE;
    else if (rc==ESP_OK) { rc=nvs_erase_key(h,key); if (rc==ESP_ERR_NVS_NOT_FOUND) rc=ESP_OK; }
    if (rc==ESP_OK) rc=nvs_commit(h);
    nvs_close(h); return rc;
}
#endif
