#pragma once

/*
 * microSD WAV segment sink for the P4 master recorder.
 *
 * Owns the open recording file, writes PCM stereo/16-bit segments under
 * /sd/recordings, rolls to a new segment on a sample-rate change or the 1 GiB
 * cap, checkpoints the WAV sizes periodically for crash recovery, and finalizes
 * a segment by patching sizes, syncing and atomically renaming .wav.part ->
 * .wav. All FAT work is serialised through sd_io_gate. Only the low-priority
 * writer task uses this; no audio producer touches it.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RECORDER_SINK_DIR      "/sd/recordings"
#define AUDIO_RECORDER_SINK_PATH_MAX 96u

/* Data-byte cap per segment (1 GiB), safely below the 4 GiB RIFF/FAT ceiling. */
#define AUDIO_RECORDER_SEGMENT_DATA_MAX (1024ull * 1024ull * 1024ull)

/* One card write instead of a burst of tiny ones. 64 KiB is ~0.37 s of audio
 * at 44.1 kHz, so an unexpected power loss can lose at most that much of the
 * tail; a checkpoint or stop flushes it first. */
#define AUDIO_RECORDER_SINK_STAGE_BYTES (64u * 1024u)

typedef struct {
    FILE    *fp;
    uint32_t sample_rate;   /* rate of the current segment */
    uint32_t boot_id;
    uint32_t session;
    uint32_t segment;       /* segment index within the session */
    uint64_t data_bytes;    /* PCM bytes in the current segment */
    char     part_path[AUDIO_RECORDER_SINK_PATH_MAX];
    bool     is_open;
    /* Write staging. The recorder produces 1 KiB blocks, but newlib's default
     * stdio buffer is 128 B, so each of those became eight tiny writes down to
     * FATFS and on to a card that punishes small scattered I/O. Blocks are
     * accumulated here and handed over in one large sequential write instead. */
    uint8_t *stage;
    size_t   stage_len;
    size_t   stage_cap;
} audio_recorder_sink_t;

/* Verify /sd is mounted, ensure the recordings directory exists and report the
 * free byte count. Returns ESP_ERR_NOT_FOUND when /sd is not mounted. */
esp_err_t audio_recorder_sink_prepare(uint64_t *out_free_bytes);

/* Open the first segment of a session (segment 0) and write the 44-byte
 * placeholder header. */
esp_err_t audio_recorder_sink_open(audio_recorder_sink_t *s, uint32_t sample_rate,
                                   uint32_t boot_id, uint32_t session);

/* Append one rendered stereo block. Finalizes the current segment and opens the
 * next one on a sample-rate change or when the 1 GiB cap would be exceeded. */
esp_err_t audio_recorder_sink_write_block(audio_recorder_sink_t *s,
                                          const int16_t *samples, uint32_t frames,
                                          uint32_t sample_rate);

/* Patch the WAV RIFF/data sizes at the file head and fsync without closing, so
 * a sudden power loss leaves a bounded, mountable file. */
esp_err_t audio_recorder_sink_checkpoint(audio_recorder_sink_t *s);

/* Finalize the current segment: patch sizes, fsync, close and atomically rename
 * .wav.part -> .wav. Idempotent when nothing is open. */
esp_err_t audio_recorder_sink_finalize(audio_recorder_sink_t *s);

/* Report free bytes on /sd (thin wrapper over esp_vfs_fat_info). */
esp_err_t audio_recorder_sink_free_bytes(uint64_t *out_free_bytes);

/* Boot recovery: scan /sd/recordings for orphan *.wav.part files left by a crash
 * or power loss, truncate each to whole stereo frames, patch its WAV sizes and
 * rename it to *.recovered.wav. Empty placeholders are removed. Never rewrites an
 * already-final .wav. Safe to call when /sd is absent (no-op). */
esp_err_t audio_recorder_sink_recover_orphans(void);

#ifdef __cplusplus
}
#endif
