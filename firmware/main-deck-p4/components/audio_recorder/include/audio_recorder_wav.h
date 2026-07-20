#pragma once

/*
 * Pure, dependency-free WAV helpers for the P4 master recorder.
 *
 * These functions only manipulate byte buffers, file sizes and file names; they
 * perform no file, allocation or real-time-audio work, so they compile
 * unchanged on the firmware and the host test target. The recorder's canonical
 * on-card format is PCM, stereo, signed 16-bit little-endian.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RECORDER_WAV_HEADER_BYTES 44u
#define AUDIO_RECORDER_WAV_CHANNELS     2u
#define AUDIO_RECORDER_WAV_BITS         16u
/* Bytes per stereo frame (two signed 16-bit samples). */
#define AUDIO_RECORDER_WAV_FRAME_BYTES  4u

/* Build a canonical 44-byte WAV header describing `data_bytes` of PCM payload at
 * `sample_rate` Hz into `out`. `out` must hold AUDIO_RECORDER_WAV_HEADER_BYTES. */
void audio_recorder_wav_build_header(uint8_t out[AUDIO_RECORDER_WAV_HEADER_BYTES],
                                     uint32_t sample_rate,
                                     uint32_t data_bytes);

/* Patch only the two size fields (RIFF chunk size and data size) of an existing
 * 44-byte header in place, e.g. when finalizing or checkpointing a segment. */
void audio_recorder_wav_patch_sizes(uint8_t header[AUDIO_RECORDER_WAV_HEADER_BYTES],
                                    uint32_t data_bytes);

/* Format a `.part` segment file name of the form
 *   REC_B<boot_id>_<session>_<segment>_<sample_rate>Hz.wav.part
 * into `out`. Returns the number of characters written (excluding the NUL), or
 * 0 if the arguments are invalid or the buffer is too small (out is emptied). */
int audio_recorder_wav_format_segment(char *out, size_t out_len,
                                      uint32_t boot_id, uint32_t session,
                                      uint32_t segment, uint32_t sample_rate);

/* Compute the number of valid PCM data bytes recoverable from a `.part` file of
 * `file_size` bytes: drop the 44-byte header and truncate to a whole stereo
 * frame. Returns false (and leaves *out_data_bytes unset) if the file cannot
 * hold at least one complete frame. */
bool audio_recorder_wav_recover_data_bytes(uint64_t file_size,
                                           uint32_t *out_data_bytes);

#ifdef __cplusplus
}
#endif
