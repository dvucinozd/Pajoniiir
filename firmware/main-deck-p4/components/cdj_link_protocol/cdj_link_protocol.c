#include "cdj_link_protocol.h"

#include <stdbool.h>
#include <string.h>

#define FNV1A_OFFSET 2166136261u
#define FNV1A_PRIME  16777619u

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    cdj_link_track_manifest_t manifest;
} cdj_link_manifest_wire_t;
#pragma pack(pop)

static void copy_str(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static bool manifest_valid(const cdj_link_track_manifest_t *manifest)
{
    if (!manifest) {
        return false;
    }
    if (manifest->track_key == 0 || manifest->audio_size == 0 || manifest->dat_size == 0) {
        return false;
    }
    if (manifest->has_ext && manifest->ext_size == 0) {
        return false;
    }
    return true;
}

static bool fixed_str_terminated(const char *s, size_t len)
{
    if (!s || len == 0) {
        return false;
    }
    return memchr(s, '\0', len) != NULL;
}

uint32_t cdj_link_track_key(uint32_t rekordbox_track_id, const char *audio_path)
{
    if (rekordbox_track_id != 0) {
        return rekordbox_track_id;
    }

    uint32_t h = FNV1A_OFFSET;
    const unsigned char *p = (const unsigned char *)(audio_path ? audio_path : "");
    while (*p) {
        h ^= (uint32_t)(*p++);
        h *= FNV1A_PRIME;
    }
    return h == 0 ? 1u : h;
}

cdj_link_result_t cdj_link_library_size(uint32_t count, size_t *out_size)
{
    if (!out_size || count > CDJ_LINK_MAX_TRACKS) {
        return CDJ_LINK_ERR_BOUNDS;
    }
    *out_size = sizeof(cdj_link_library_header_t) +
                (size_t)count * sizeof(cdj_link_track_record_t);
    return CDJ_LINK_OK;
}

cdj_link_result_t cdj_link_library_encode(uint8_t *dst,
                                           size_t dst_len,
                                           const cdj_link_track_record_t *records,
                                           uint32_t count,
                                           size_t *out_written)
{
    size_t need = 0;
    if (!dst || !out_written || (count > 0 && !records)) {
        return CDJ_LINK_ERR_INVALID;
    }
    cdj_link_result_t rc = cdj_link_library_size(count, &need);
    if (rc != CDJ_LINK_OK) {
        return rc;
    }
    if (dst_len < need) {
        return CDJ_LINK_ERR_BOUNDS;
    }

    cdj_link_library_header_t header = {
        .magic = CDJ_LINK_LIBRARY_MAGIC,
        .version = CDJ_LINK_PROTOCOL_VERSION,
        .header_size = sizeof(cdj_link_library_header_t),
        .record_size = sizeof(cdj_link_track_record_t),
        .reserved = 0,
        .count = count,
    };
    memcpy(dst, &header, sizeof(header));
    if (count > 0) {
        memcpy(dst + sizeof(header), records, (size_t)count * sizeof(*records));
    }
    *out_written = need;
    return CDJ_LINK_OK;
}

cdj_link_result_t cdj_link_library_decode(const uint8_t *src,
                                           size_t src_len,
                                           cdj_link_library_view_t *out_view)
{
    if (!src || !out_view || src_len < sizeof(cdj_link_library_header_t)) {
        return CDJ_LINK_ERR_INVALID;
    }

    cdj_link_library_header_t header;
    memcpy(&header, src, sizeof(header));
    if (header.magic != CDJ_LINK_LIBRARY_MAGIC) {
        return CDJ_LINK_ERR_BAD_MAGIC;
    }
    if (header.version != CDJ_LINK_PROTOCOL_VERSION) {
        return CDJ_LINK_ERR_VERSION;
    }
    if (header.header_size != sizeof(cdj_link_library_header_t) ||
        header.record_size != sizeof(cdj_link_track_record_t)) {
        return CDJ_LINK_ERR_INVALID;
    }

    size_t need = 0;
    cdj_link_result_t rc = cdj_link_library_size(header.count, &need);
    if (rc != CDJ_LINK_OK) {
        return rc;
    }
    if (src_len < need) {
        return CDJ_LINK_ERR_BOUNDS;
    }

    out_view->count = header.count;
    out_view->records = (const cdj_link_track_record_t *)(const void *)(src + sizeof(header));
    return CDJ_LINK_OK;
}

cdj_link_result_t cdj_link_manifest_encode(uint8_t *dst,
                                            size_t dst_len,
                                            const cdj_link_track_manifest_t *manifest,
                                            size_t *out_written)
{
    if (!dst || !out_written || !manifest_valid(manifest)) {
        return CDJ_LINK_ERR_INVALID;
    }
    if (dst_len < sizeof(cdj_link_manifest_wire_t)) {
        return CDJ_LINK_ERR_BOUNDS;
    }

    cdj_link_manifest_wire_t wire = {
        .magic = CDJ_LINK_MANIFEST_MAGIC,
        .version = CDJ_LINK_PROTOCOL_VERSION,
        .record_size = sizeof(cdj_link_track_manifest_t),
        .manifest = *manifest,
    };
    memcpy(dst, &wire, sizeof(wire));
    *out_written = sizeof(wire);
    return CDJ_LINK_OK;
}

cdj_link_result_t cdj_link_manifest_decode(const uint8_t *src,
                                            size_t src_len,
                                            cdj_link_track_manifest_t *out_manifest)
{
    if (!src || !out_manifest || src_len < sizeof(cdj_link_manifest_wire_t)) {
        return CDJ_LINK_ERR_INVALID;
    }

    cdj_link_manifest_wire_t wire;
    memcpy(&wire, src, sizeof(wire));
    if (wire.magic != CDJ_LINK_MANIFEST_MAGIC) {
        return CDJ_LINK_ERR_BAD_MAGIC;
    }
    if (wire.version != CDJ_LINK_PROTOCOL_VERSION) {
        return CDJ_LINK_ERR_VERSION;
    }
    if (wire.record_size != sizeof(cdj_link_track_manifest_t) || !manifest_valid(&wire.manifest)) {
        return CDJ_LINK_ERR_INVALID;
    }
    *out_manifest = wire.manifest;
    return CDJ_LINK_OK;
}

cdj_link_result_t cdj_link_discovery_init(cdj_link_discovery_packet_t *packet,
                                           const char *peer_id,
                                           const char *name,
                                           uint16_t port,
                                           uint32_t track_count)
{
    if (!packet || port == 0 || track_count > CDJ_LINK_MAX_TRACKS) {
        return CDJ_LINK_ERR_INVALID;
    }
    memset(packet, 0, sizeof(*packet));
    packet->magic = CDJ_LINK_DISCOVERY_MAGIC;
    packet->version = CDJ_LINK_PROTOCOL_VERSION;
    packet->record_size = sizeof(*packet);
    packet->port = port;
    packet->track_count = track_count;
    copy_str(packet->peer_id, sizeof(packet->peer_id), peer_id);
    copy_str(packet->name, sizeof(packet->name), name);
    return CDJ_LINK_OK;
}

cdj_link_result_t cdj_link_discovery_validate(const cdj_link_discovery_packet_t *packet)
{
    if (!packet) {
        return CDJ_LINK_ERR_INVALID;
    }
    if (packet->magic != CDJ_LINK_DISCOVERY_MAGIC) {
        return CDJ_LINK_ERR_BAD_MAGIC;
    }
    if (packet->version != CDJ_LINK_PROTOCOL_VERSION) {
        return CDJ_LINK_ERR_VERSION;
    }
    if (packet->record_size != sizeof(*packet) || packet->port == 0 ||
        packet->track_count > CDJ_LINK_MAX_TRACKS) {
        return CDJ_LINK_ERR_INVALID;
    }
    if (!fixed_str_terminated(packet->peer_id, sizeof(packet->peer_id)) ||
        !fixed_str_terminated(packet->name, sizeof(packet->name))) {
        return CDJ_LINK_ERR_INVALID;
    }
    return CDJ_LINK_OK;
}
