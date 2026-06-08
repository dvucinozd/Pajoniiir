#pragma once

#include <stddef.h>
#include <stdint.h>

#define CDJ_LINK_PROTOCOL_VERSION 1u
#define CDJ_LINK_MAX_TRACKS       1024u
#define CDJ_LINK_PEER_ID_LEN      16u
#define CDJ_LINK_NAME_LEN         32u
#define CDJ_LINK_DISCOVERY_PORT   42424u

#define CDJ_LINK_LIBRARY_MAGIC    0x314C4A43u /* "CJL1" little-endian */
#define CDJ_LINK_MANIFEST_MAGIC   0x314D4A43u /* "CJM1" little-endian */
#define CDJ_LINK_DISCOVERY_MAGIC  0x31444A43u /* "CJD1" little-endian */

typedef enum {
    MEDIA_SOURCE_LOCAL_USB = 0,
    MEDIA_SOURCE_REMOTE_LINK = 1,
} media_source_t;

typedef enum {
    CDJ_LINK_OK = 0,
    CDJ_LINK_ERR_INVALID = -1,
    CDJ_LINK_ERR_BOUNDS = -2,
    CDJ_LINK_ERR_BAD_MAGIC = -3,
    CDJ_LINK_ERR_VERSION = -4,
} cdj_link_result_t;

#pragma pack(push, 1)
typedef struct {
    uint32_t track_key;
    uint32_t rekordbox_track_id;
    uint16_t bpm;
    uint32_t duration_ms;
    char title[96];
    char artist[64];
    char album[64];
} cdj_link_track_record_t;

typedef struct {
    uint32_t track_key;
    uint32_t audio_size;
    uint32_t dat_size;
    uint32_t ext_size;
    uint8_t has_ext;
} cdj_link_track_manifest_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint16_t record_size;
    uint16_t reserved;
    uint32_t count;
} cdj_link_library_header_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint16_t port;
    uint32_t track_count;
    char peer_id[CDJ_LINK_PEER_ID_LEN];
    char name[CDJ_LINK_NAME_LEN];
} cdj_link_discovery_packet_t;
#pragma pack(pop)

typedef struct {
    uint32_t count;
    const cdj_link_track_record_t *records;
} cdj_link_library_view_t;

typedef struct {
    media_source_t source;
    uint32_t track_key;
    char audio_path[272];
    char dat_path[272];
    char ext_path[272];
    uint32_t duration_ms;
    uint16_t bpm;
    uint8_t waveform_low[400];
    uint8_t has_waveform;
    uint32_t pvbr[400];
    uint8_t has_pvbr;
} media_loaded_track_t;

uint32_t cdj_link_track_key(uint32_t rekordbox_track_id, const char *audio_path);

cdj_link_result_t cdj_link_library_size(uint32_t count, size_t *out_size);
cdj_link_result_t cdj_link_library_encode(uint8_t *dst,
                                           size_t dst_len,
                                           const cdj_link_track_record_t *records,
                                           uint32_t count,
                                           size_t *out_written);
cdj_link_result_t cdj_link_library_decode(const uint8_t *src,
                                           size_t src_len,
                                           cdj_link_library_view_t *out_view);

cdj_link_result_t cdj_link_manifest_encode(uint8_t *dst,
                                            size_t dst_len,
                                            const cdj_link_track_manifest_t *manifest,
                                            size_t *out_written);
cdj_link_result_t cdj_link_manifest_decode(const uint8_t *src,
                                            size_t src_len,
                                            cdj_link_track_manifest_t *out_manifest);

cdj_link_result_t cdj_link_discovery_init(cdj_link_discovery_packet_t *packet,
                                           const char *peer_id,
                                           const char *name,
                                           uint16_t port,
                                           uint32_t track_count);
cdj_link_result_t cdj_link_discovery_validate(const cdj_link_discovery_packet_t *packet);
