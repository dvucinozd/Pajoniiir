/*
 * rekordbox_anlz.c  —  Rekordbox ANLZ file parser
 *
 * Parses ANLZ0000.DAT (PPTH, PVBR, PQTZ, PWAV, PCOB) and
 * ANLZ0000.EXT (PWV3) from a Rekordbox USB drive.
 *
 * All multi-byte fields in the file are big-endian.
 * This code runs on both ESP32-P4 (via ESP-IDF VFS) and PC (ANLZ_STANDALONE_TEST).
 */

#include "rekordbox_anlz.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef ANLZ_STANDALONE_TEST
#  include <dirent.h>
#  include <sys/stat.h>
#endif
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "anlz";
#define ANLZ_MAX_BEATS 0xFFFFu

/* ── Big-endian read helpers ─────────────────────────────────────────────── */

static inline uint16_t read_be16(FILE *fp)
{
    uint8_t b[2];
    if (fread(b, 1, 2, fp) != 2) return 0;
    return (uint16_t)((b[0] << 8) | b[1]);
}

static inline uint32_t read_be32(FILE *fp)
{
    uint8_t b[4];
    if (fread(b, 1, 4, fp) != 4) return 0;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
}

/* ── Token search ────────────────────────────────────────────────────────── */

/* Scan forward from the current position looking for a matching 4-byte tag.
 * Uses a single-byte sliding window — reads strictly forward, never seeks back.
 * This is safe on slow/remote storage (USB drives, network) where backwards
 * fseek() would defeat FILE buffering and cause a seek per step.
 *
 * On success the file pointer is positioned immediately after the tag ID
 * (i.e. at the header_size field).
 * Returns true if found, false on EOF/error. */
static bool find_tag(FILE *fp, uint32_t target)
{
    uint32_t window = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        window = (window << 8) | (uint8_t)c;
        if (window == target) return true;
    }
    return false;
}

/* ── PPTH parser ─────────────────────────────────────────────────────────── *
 *
 * Byte layout after the 4-byte tag:
 *   4B  header_size  (typically 20)
 *   4B  segment_size (total including header; path data follows header)
 *   4B  flags / padding
 *   4B  path_length  (number of bytes of UTF-16 BE data that follow, incl. NUL)
 *   N×2B  UTF-16 BE characters
 *
 * We extract printable ASCII characters (high byte == 0x00, low byte 0x20–0x7E)
 * to produce a clean UTF-8/ASCII path.  Non-ASCII track names are substituted
 * with '?'; directory separators '\' are converted to '/'.
 */
static esp_err_t parse_ppth(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp); /* bytes in header incl. tag+hdr_size+seg_size */
    uint32_t segment_size = read_be32(fp); /* total segment including header              */

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PPTH: bad sizes hdr=%u seg=%u", header_size, segment_size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Skip remaining header bytes (flags + path_length field are part of header) */
    /* Reference: after tag (4B) + header_size field (4B) + segment_size field (4B)
     * we have already consumed 8B past the tag.  The header_size counts from the
     * tag start, so remaining header bytes = header_size - 4 (tag) - 4 (hdr) - 4 (seg) */
    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size; /* byte count of UTF-16 path data */
    if (data_len == 0 || data_len > (ANLZ_PATH_MAX * 2u)) {
        ANLZ_LOGE(TAG, "PPTH: path data length %u out of range", data_len);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Read UTF-16 BE pairs, keep printable ASCII characters only */
    size_t out_idx = 0;
    for (uint32_t i = 0; i + 1 < data_len; i += 2) {
        uint8_t hi = (uint8_t)fgetc(fp);
        uint8_t lo = (uint8_t)fgetc(fp);
        if (hi == 0x00 && lo == 0x00) break; /* NUL terminator */
        if (out_idx >= ANLZ_PATH_MAX - 1) break;

        if (hi == 0x00) {
            /* ASCII-range code point */
            char c = (char)lo;
            if (c == '\\') c = '/'; /* Windows path separator → POSIX */
            out->audio_path[out_idx++] = c;
        } else {
            /* Non-BMP or high Unicode — substitute */
            out->audio_path[out_idx++] = '?';
        }
    }
    out->audio_path[out_idx] = '\0';

    ANLZ_LOGI(TAG, "PPTH: \"%s\"", out->audio_path);
    return ESP_OK;
}

/* ── PVBR parser ─────────────────────────────────────────────────────────── *
 *
 * Byte layout after tag:
 *   4B  header_size
 *   4B  segment_size
 *   (header_size − 12) bytes of padding  [typically 8 bytes → skip 4 more]
 *   400 × 4B  VBR seek offsets (big-endian uint32)
 */
static esp_err_t parse_pvbr(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PVBR: bad sizes");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    uint32_t entries  = data_len / 4u;
    if (entries > ANLZ_VBR_TABLE_LEN) entries = ANLZ_VBR_TABLE_LEN;

    for (uint32_t i = 0; i < entries; i++) {
        out->vbr[i] = read_be32(fp);
    }
    out->has_vbr = (entries > 0);

    ANLZ_LOGI(TAG, "PVBR: %u seek entries. First 10: %u, %u, %u, %u, %u, %u, %u, %u, %u, %u",
              entries, out->vbr[0], out->vbr[1], out->vbr[2], out->vbr[3], out->vbr[4],
              out->vbr[5], out->vbr[6], out->vbr[7], out->vbr[8], out->vbr[9]);
    return ESP_OK;
}

/* ── PQTZ parser ─────────────────────────────────────────────────────────── *
 *
 * Byte layout after tag:
 *   4B  header_size
 *   4B  segment_size
 *   (header_size − 12) bytes padding
 *   N × 8B  beat entries:
 *     2B beat_phase (uint16 BE)
 *     2B bpm_x100  (uint16 BE)
 *     4B time_ms   (uint32 BE)
 */
static esp_err_t parse_pqtz(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PQTZ: bad sizes");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    uint32_t count    = data_len / 8u;

    if (count == 0) {
        ANLZ_LOGW(TAG, "PQTZ: no beat entries");
        return ESP_OK;
    }

    uint32_t stored_count = count > ANLZ_MAX_BEATS ? ANLZ_MAX_BEATS : count;
    out->beats = (anlz_beat_t *)malloc(stored_count * sizeof(anlz_beat_t));
    if (!out->beats) {
        ANLZ_LOGE(TAG, "PQTZ: malloc failed (%u entries)", stored_count);
        return ESP_ERR_NO_MEM;
    }
    out->beat_count = (uint16_t)stored_count;

    for (uint32_t i = 0; i < stored_count; i++) {
        out->beats[i].beat_phase = read_be16(fp);
        out->beats[i].bpm_x100  = read_be16(fp);
        out->beats[i].time_ms   = read_be32(fp);
    }
    if (count > stored_count) {
        uint32_t skipped = count - stored_count;
        if (fseek(fp, (long)(skipped * 8u), SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;
        ANLZ_LOGW(TAG, "PQTZ: truncated beat grid from %u to %u entries",
                  count, stored_count);
    }

    /* BPM from first entry */
    if (stored_count > 0 && out->beats[0].bpm_x100 > 0) {
        out->bpm = (uint16_t)((out->beats[0].bpm_x100 + 50u) / 100u);
    }

    ANLZ_LOGI(TAG, "PQTZ: %u beats, BPM=%u (raw=%u)", stored_count, out->bpm,
              stored_count > 0 ? out->beats[0].bpm_x100 : 0);
    return ESP_OK;
}

/* ── PWAV parser ─────────────────────────────────────────────────────────── *
 *
 * Byte layout after tag:
 *   4B  header_size
 *   4B  segment_size
 *   (header_size − 12) bytes padding  [often 8 B → skip 4 more]
 *   400B waveform data (1B per column: bits[7:5]=color index, bits[4:0]=height)
 */
static esp_err_t parse_pwav(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PWAV: bad sizes");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    uint32_t bytes    = data_len < ANLZ_WAVEFORM_LOW_LEN ? data_len : ANLZ_WAVEFORM_LOW_LEN;

    size_t n = fread(out->waveform_low, 1, bytes, fp);
    out->has_waveform_low = (n == bytes);

    ANLZ_LOGI(TAG, "PWAV: %zu/%u bytes", n, ANLZ_WAVEFORM_LOW_LEN);
    return out->has_waveform_low ? ESP_OK : ESP_FAIL;
}

/* ── PCOB / PCPT parser ──────────────────────────────────────────────────── *
 *
 * PCOB is a container tag.  Its data section contains one or more PCPT
 * sub-records, each 56 bytes long.  We extract up to ANLZ_MAX_CUES cues.
 *
 * PCOB layout after tag:
 *   4B  header_size
 *   4B  segment_size
 *   (header_size − 12) bytes padding
 *   [PCPT sub-records, each 56B]
 *
 * Each PCPT (56 bytes):
 *   Byte 0     : entry_type  (0x01 = single, 0x02 = loop)
 *   Byte 1     : index       (0–7)
 *   Bytes 2–3  : unknown / color
 *   Bytes 4–7  : start_ms    (uint32 BE)
 *   Bytes 8–11 : end_ms      (uint32 BE, loops only)
 *   Bytes 12–55: name, color info (unused here)
 *
 * Note: The exact layout varies slightly between Rekordbox versions.
 * This follows the spectran/rekordbox + DeepSymmetry specifications.
 */
static esp_err_t parse_pcob(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PCOB: bad sizes");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len   = segment_size - header_size;
    uint32_t pcpt_count = data_len / 56u;

    out->cue_count = 0;

    for (uint32_t i = 0; i < pcpt_count; i++) {
        /* Read 56 bytes for this PCPT entry */
        uint8_t buf[56];
        if (fread(buf, 1, 56, fp) != 56) break;

        uint8_t entry_type = buf[0];
        uint8_t index      = buf[1];

        if (index >= ANLZ_MAX_CUES) continue; /* ignore out-of-range slots */

        uint32_t start_ms = ((uint32_t)buf[4]  << 24) | ((uint32_t)buf[5]  << 16) |
                            ((uint32_t)buf[6]  <<  8) |  (uint32_t)buf[7];
        uint32_t end_ms   = ((uint32_t)buf[8]  << 24) | ((uint32_t)buf[9]  << 16) |
                            ((uint32_t)buf[10] <<  8) |  (uint32_t)buf[11];

        anlz_cue_t *cue = &out->cues[out->cue_count];
        cue->type     = (entry_type == 2) ? ANLZ_CUE_LOOP : ANLZ_CUE_SINGLE;
        cue->index    = index;
        cue->start_ms = start_ms;
        cue->end_ms   = (cue->type == ANLZ_CUE_LOOP) ? end_ms : 0u;

        out->cue_count++;
        if (out->cue_count >= ANLZ_MAX_CUES) break;
    }

    ANLZ_LOGI(TAG, "PCOB: %u cue/loop entries", out->cue_count);
    return ESP_OK;
}

/* ── PWV3 parser ─────────────────────────────────────────────────────────── *
 *
 * Byte layout after tag (same as PWAV but data can be up to 60 000 bytes):
 *   4B  header_size
 *   4B  segment_size
 *   (header_size − 12) bytes padding
 *   N×1B waveform data (N = segment_size − header_size)
 */
static esp_err_t parse_pwv3(FILE *fp, anlz_metadata_t *meta)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PWV3: bad sizes");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    if (data_len == 0) {
        ANLZ_LOGW(TAG, "PWV3: empty waveform");
        return ESP_OK;
    }
    if (data_len > ANLZ_WAVEFORM_HIGH_MAX) {
        data_len = ANLZ_WAVEFORM_HIGH_MAX;
    }

    /* Free any previous allocation (safe double-parse guard) */
    if (meta->waveform_high) {
        free(meta->waveform_high);
        meta->waveform_high     = NULL;
        meta->waveform_high_len = 0;
    }

    meta->waveform_high = (uint8_t *)malloc(data_len);
    if (!meta->waveform_high) {
        ANLZ_LOGE(TAG, "PWV3: malloc %u bytes failed", data_len);
        return ESP_ERR_NO_MEM;
    }

    size_t n = fread(meta->waveform_high, 1, data_len, fp);
    meta->waveform_high_len = (uint32_t)n;

    ANLZ_LOGI(TAG, "PWV3: %u bytes high-res waveform", (unsigned)n);
    return (n == data_len) ? ESP_OK : ESP_FAIL;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t anlz_parse_dat(const char *dat_path, anlz_metadata_t *out)
{
    if (!dat_path || !out) return ESP_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(dat_path, "rb");
    if (!fp) {
        ANLZ_LOGE(TAG, "Cannot open: %s", dat_path);
        return ESP_ERR_NOT_FOUND;
    }

    ANLZ_LOGI(TAG, "Parsing DAT: %s", dat_path);

    /* Tags we need to find — use a bitmask to track what we got */
    uint8_t found = 0;
#define GOT_PPTH  (1u << 0)
#define GOT_PVBR  (1u << 1)
#define GOT_PQTZ  (1u << 2)
#define GOT_PWAV  (1u << 3)
#define GOT_PCOB  (1u << 4)

    /* For each tag we do a full seek-from-start so that tags can appear in
     * any order (Rekordbox typically writes them in a fixed order, but we
     * must not rely on that). */
    const struct { uint32_t id; uint8_t flag; } wanted[] = {
        { ANLZ_TAG_PPTH, GOT_PPTH },
        { ANLZ_TAG_PVBR, GOT_PVBR },
        { ANLZ_TAG_PQTZ, GOT_PQTZ },
        { ANLZ_TAG_PWAV, GOT_PWAV },
        { ANLZ_TAG_PCOB, GOT_PCOB },
    };

    for (size_t i = 0; i < sizeof(wanted)/sizeof(wanted[0]); i++) {
        rewind(fp);
        if (!find_tag(fp, wanted[i].id)) {
            ANLZ_LOGW(TAG, "Tag 0x%08X not found", wanted[i].id);
            continue;
        }
        esp_err_t rc = ESP_FAIL;
        switch (wanted[i].id) {
            case ANLZ_TAG_PPTH: rc = parse_ppth(fp, out); break;
            case ANLZ_TAG_PVBR: rc = parse_pvbr(fp, out); break;
            case ANLZ_TAG_PQTZ: rc = parse_pqtz(fp, out); break;
            case ANLZ_TAG_PWAV: rc = parse_pwav(fp, out); break;
            case ANLZ_TAG_PCOB: rc = parse_pcob(fp, out); break;
        }
        if (rc == ESP_OK) {
            found |= wanted[i].flag;
        } else {
            ANLZ_LOGW(TAG, "Tag 0x%08X parse error: %d", wanted[i].id, rc);
        }
    }

    fclose(fp);

    if (!(found & GOT_PPTH)) {
        ANLZ_LOGE(TAG, "PPTH (audio path) missing — DAT invalid");
        anlz_free(out);
        return ESP_ERR_NOT_FOUND;
    }

    ANLZ_LOGI(TAG, "DAT parsed OK: bpm=%u beats=%u cues=%u vbr=%d wav=%d",
              out->bpm, out->beat_count, out->cue_count,
              out->has_vbr, out->has_waveform_low);
    return ESP_OK;
}

esp_err_t anlz_parse_ext(const char *ext_path, anlz_metadata_t *meta)
{
    if (!ext_path || !meta) return ESP_ERR_INVALID_ARG;

    FILE *fp = fopen(ext_path, "rb");
    if (!fp) {
        ANLZ_LOGE(TAG, "Cannot open EXT: %s", ext_path);
        return ESP_ERR_NOT_FOUND;
    }

    ANLZ_LOGI(TAG, "Parsing EXT: %s", ext_path);

    rewind(fp);
    esp_err_t rc = ESP_ERR_NOT_FOUND;
    if (find_tag(fp, ANLZ_TAG_PWV3)) {
        rc = parse_pwv3(fp, meta);
    } else {
        ANLZ_LOGW(TAG, "PWV3 tag not found in EXT file");
    }

    fclose(fp);
    return rc;
}

void anlz_free(anlz_metadata_t *meta)
{
    if (!meta) return;

    if (meta->beats) {
        free(meta->beats);
        meta->beats      = NULL;
        meta->beat_count = 0;
    }
    if (meta->waveform_high) {
        free(meta->waveform_high);
        meta->waveform_high     = NULL;
        meta->waveform_high_len = 0;
    }
}

/* ── anlz_walk_usbanlz ──────────────────────────────────────────────────────
 *
 * Walk PIONEER/USBANLZ/ and call cb() for every folder containing ANLZ0000.DAT.
 *
 * IMPORTANT: Real Rekordbox USB drives use HASH-BASED folder names under USBANLZ
 * (e.g. PIONEER/USBANLZ/P000/00000832/ANLZ0000.DAT).  The hash values are
 * internal Rekordbox track IDs — they are NOT derived from the audio file path.
 * To map audio_path → ANLZ folder, you must walk all ANLZ files and read PPTH.
 *
 * Observed folder structure (tested with real Rekordbox USB, 308 tracks):
 *   PIONEER/USBANLZ/P000/00000832/ANLZ0000.DAT  (PPTH path = /Contents/artist/track.mp3)
 *   PIONEER/USBANLZ/P000/00000D18/ANLZ0000.DAT
 *   PIONEER/USBANLZ/P001/...
 *   ...
 *
 * NOTE: Requires ESP-IDF VFS + USB host to be mounted.  Phase 6 only.
 *       On bare-metal / pre-Phase-6: use anlz_parse_dat() directly with known path.
 */
esp_err_t anlz_walk_usbanlz(const char      *usbanlz_root,
                              anlz_folder_cb_t cb,
                              void            *user_data)
{
#ifdef ANLZ_STANDALONE_TEST
    /* PC / test implementation using POSIX dirent */
    if (!usbanlz_root || !cb) return ESP_ERR_INVALID_ARG;

    DIR *root_dir = opendir(usbanlz_root);
    if (!root_dir) {
        ANLZ_LOGE(TAG, "Cannot open USBANLZ root: %s", usbanlz_root);
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *p_entry;
    while ((p_entry = readdir(root_dir)) != NULL) {
        if (p_entry->d_name[0] == '.') continue;

        char l1[512];
        snprintf(l1, sizeof(l1), "%s/%s", usbanlz_root, p_entry->d_name);

        DIR *l1_dir = opendir(l1);
        if (!l1_dir) continue;

        struct dirent *l2_entry;
        while ((l2_entry = readdir(l1_dir)) != NULL) {
            if (l2_entry->d_name[0] == '.') continue;

            char l2[512];
            snprintf(l2, sizeof(l2), "%s/%s", l1, l2_entry->d_name);

            char dat[520];
            snprintf(dat, sizeof(dat), "%s/ANLZ0000.DAT", l2);

            FILE *fp = fopen(dat, "rb");
            if (!fp) continue;
            fclose(fp);

            /* Read just PPTH from this DAT to get audio_path */
            anlz_metadata_t tmp;
            esp_err_t rc = anlz_parse_dat(dat, &tmp);
            if (rc == ESP_OK) {
                bool cont = cb(l2, tmp.audio_path, user_data);
                anlz_free(&tmp);
                if (!cont) {
                    closedir(l1_dir);
                    closedir(root_dir);
                    return ESP_OK;
                }
            }
        }
        closedir(l1_dir);
    }
    closedir(root_dir);
    return ESP_OK;

#else
    /* ESP-IDF implementation — Phase 6: use VFS readdir */
    /* TODO Phase 6: implement using esp_vfs_fat / USB host directory traversal */
    (void)usbanlz_root; (void)cb; (void)user_data;
    ANLZ_LOGW(TAG, "anlz_walk_usbanlz: not implemented (Phase 6)");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
