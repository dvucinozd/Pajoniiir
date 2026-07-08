#pragma once

/* P4 controller profile manager.
 *
 * Scans the SD/TF card for compiled controller profiles (S3CP `.s3bin`,
 * see docs/CONTROLLER_PROFILE_SCHEMA.md), validates their headers, and keeps a
 * registry the P4 uses to pick a profile for a connected controller and (later
 * phases) transfer it to the S3.
 *
 * The pure functions (header parse, directory scan, registry match) carry no
 * ESP-IDF logging or sdkconfig dependency so the host test harness compiles
 * them directly. The S3CP binary layout mirrors the authoritative S3-side
 * parser in firmware/control-board-s3/components/controller_profile/; the
 * controller_profile_manager host test cross-checks both against the same
 * committed FLX4 fixture so the two format readers cannot drift.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CPM_MAX_PROFILES     16
#define CPM_ID_MAX           40
#define CPM_PATH_MAX         160
#define CPM_MAX_PROFILE_SIZE 16384

/* S3CP header layout (little-endian). Must match the schema/compiler. */
#define CPM_MAGIC        "S3CP"
#define CPM_VERSION      1
#define CPM_HEADER_SIZE  32

typedef struct {
    char id[CPM_ID_MAX];      /* directory name, e.g. "pioneer_ddj_flx4" */
    char path[CPM_PATH_MAX];  /* full path to profile.s3bin */
    uint16_t vid;
    uint16_t pid;
    uint16_t input_count;
    uint16_t output_count;
    uint32_t size;
    bool valid;               /* header + CRC validated */
} controller_profile_meta_t;

typedef struct {
    controller_profile_meta_t profiles[CPM_MAX_PROFILES];
    uint8_t count;
    int8_t active_index;       /* index into profiles[], -1 = none/unsupported */
    bool controller_present;   /* an S3 controller descriptor has been received */
    uint16_t connected_vid;
    uint16_t connected_pid;
} controller_profile_registry_t;

/* ── Firmware entry points (glue; sdkconfig + logging) ──────────────────────── */

/* Reset the singleton registry. */
esp_err_t controller_profile_manager_init(void);

/* Scan CONFIG_CONTROLLER_PROFILE_SD_PATH and log the discovered profiles. */
esp_err_t controller_profile_manager_scan_storage(void);

/* Read-only view of the singleton registry (for UI / web status). */
const controller_profile_registry_t *controller_profile_manager_get_registry(void);

/* Record a connected controller (fed by the S3 descriptor report in a later
 * phase) and re-select the active profile. Returns the matched index or -1. */
int controller_profile_manager_on_descriptor(uint16_t vid, uint16_t pid);

/* ── Pure helpers (host-testable, no ESP logging) ──────────────────────────── */

/* CRC-32 (IEEE 802.3, zlib-compatible). */
uint32_t controller_profile_crc32(const uint8_t *data, size_t len);

/* Validate an in-memory S3CP blob and fill the format fields of *meta
 * (vid/pid/counts/size and `valid`). Leaves id/path untouched. Returns ESP_OK
 * on a valid profile; ESP_ERR_INVALID_ARG on bad/oversize/corrupt input. */
esp_err_t controller_profile_meta_parse(const uint8_t *data, size_t len,
                                        controller_profile_meta_t *meta);

/* Scan `root` for `<name>/profile.s3bin` entries, validating each header and
 * populating `reg`. Returns ESP_OK (even if zero valid profiles are found),
 * ESP_ERR_NOT_FOUND when `root` cannot be opened, ESP_ERR_INVALID_ARG on bad
 * args. */
esp_err_t controller_profile_scan_dir(const char *root,
                                      controller_profile_registry_t *reg);

/* Exact VID/PID match against valid registry entries; index or -1. */
int controller_profile_registry_match(const controller_profile_registry_t *reg,
                                      uint16_t vid, uint16_t pid);

/* Apply a connected controller descriptor to a registry and select the active
 * profile. Returns the matched index or -1. */
int controller_profile_registry_on_descriptor(controller_profile_registry_t *reg,
                                              uint16_t vid, uint16_t pid);

#ifdef __cplusplus
}
#endif
