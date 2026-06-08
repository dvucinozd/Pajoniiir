#include "cdj_link_protocol.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_library_roundtrip(void)
{
    cdj_link_track_record_t records[2] = {
        {
            .track_key = 1001,
            .rekordbox_track_id = 1001,
            .bpm = 12800,
            .duration_ms = 361000,
            .title = "Redline",
            .artist = "Unit Test",
            .album = "Protocol",
        },
        {
            .track_key = 1002,
            .rekordbox_track_id = 1002,
            .bpm = 12450,
            .duration_ms = 242000,
            .title = "Blue Hour",
            .artist = "Unit Test",
            .album = "Protocol",
        },
    };
    uint8_t buf[1024];
    size_t written = 0;

    assert(cdj_link_library_encode(buf, sizeof(buf), records, 2, &written) == CDJ_LINK_OK);
    assert(written == sizeof(cdj_link_library_header_t) + sizeof(records));

    cdj_link_library_view_t view;
    assert(cdj_link_library_decode(buf, written, &view) == CDJ_LINK_OK);
    assert(view.count == 2);
    assert(strcmp(view.records[0].title, "Redline") == 0);
    assert(strcmp(view.records[1].artist, "Unit Test") == 0);
}

static void test_library_rejects_bad_magic(void)
{
    uint8_t buf[512];
    size_t written = 0;
    cdj_link_track_record_t record = { .track_key = 7 };

    assert(cdj_link_library_encode(buf, sizeof(buf), &record, 1, &written) == CDJ_LINK_OK);
    buf[0] ^= 0x55;

    cdj_link_library_view_t view;
    assert(cdj_link_library_decode(buf, written, &view) == CDJ_LINK_ERR_BAD_MAGIC);
}

static void test_library_rejects_excessive_count(void)
{
    cdj_link_library_header_t header = {
        .magic = CDJ_LINK_LIBRARY_MAGIC,
        .version = CDJ_LINK_PROTOCOL_VERSION,
        .header_size = sizeof(cdj_link_library_header_t),
        .record_size = sizeof(cdj_link_track_record_t),
        .count = CDJ_LINK_MAX_TRACKS + 1,
    };

    cdj_link_library_view_t view;
    assert(cdj_link_library_decode((const uint8_t *)&header, sizeof(header), &view) == CDJ_LINK_ERR_BOUNDS);
}

static void test_manifest_roundtrip_and_validation(void)
{
    cdj_link_track_manifest_t manifest = {
        .track_key = 9001,
        .audio_size = 7340032,
        .dat_size = 16384,
        .ext_size = 65536,
        .has_ext = 1,
    };
    uint8_t buf[128];
    size_t written = 0;
    cdj_link_track_manifest_t decoded;

    assert(cdj_link_manifest_encode(buf, sizeof(buf), &manifest, &written) == CDJ_LINK_OK);
    assert(cdj_link_manifest_decode(buf, written, &decoded) == CDJ_LINK_OK);
    assert(decoded.track_key == manifest.track_key);
    assert(decoded.audio_size == manifest.audio_size);
    assert(decoded.has_ext == 1);

    manifest.audio_size = 0;
    assert(cdj_link_manifest_encode(buf, sizeof(buf), &manifest, &written) == CDJ_LINK_ERR_INVALID);
}

static void test_track_key_prefers_rekordbox_id(void)
{
    assert(cdj_link_track_key(42, "/Contents/A.mp3") == 42);
    assert(cdj_link_track_key(0, "/Contents/A.mp3") == cdj_link_track_key(0, "/Contents/A.mp3"));
    assert(cdj_link_track_key(0, "/Contents/A.mp3") != cdj_link_track_key(0, "/Contents/B.mp3"));
}

static void test_discovery_rejects_non_terminated_strings(void)
{
    cdj_link_discovery_packet_t packet;
    assert(cdj_link_discovery_init(&packet, "AABBCCDDEEFF", "CDJ100S-AABB", 8080, 12) == CDJ_LINK_OK);
    assert(cdj_link_discovery_validate(&packet) == CDJ_LINK_OK);

    memset(packet.peer_id, 'X', sizeof(packet.peer_id));
    assert(cdj_link_discovery_validate(&packet) == CDJ_LINK_ERR_INVALID);

    assert(cdj_link_discovery_init(&packet, "AABBCCDDEEFF", "CDJ100S-AABB", 8080, 12) == CDJ_LINK_OK);
    memset(packet.name, 'Y', sizeof(packet.name));
    assert(cdj_link_discovery_validate(&packet) == CDJ_LINK_ERR_INVALID);
}

int main(void)
{
    test_library_roundtrip();
    test_library_rejects_bad_magic();
    test_library_rejects_excessive_count();
    test_manifest_roundtrip_and_validation();
    test_track_key_prefers_rekordbox_id();
    test_discovery_rejects_non_terminated_strings();
    puts("cdj_link_protocol tests passed");
    return 0;
}
