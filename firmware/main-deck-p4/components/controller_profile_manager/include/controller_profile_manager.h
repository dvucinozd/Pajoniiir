#pragma once

/* P4 controller profile manager.
 *
 * Scans the SD/TF card for compiled controller profiles (S3CP `.s3bin`,
 * see docs/CONTROLLER_PROFILE_SCHEMA.md), validates their headers, and keeps a
 * registry the P4 uses to pick and locally activate a profile for the directly
 * attached controller.
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
#define CPM_PROFILE_FILENAME "profile.s3bin"
#define CPM_UPLOAD_FILENAME  ".profile.s3bin.upload"
#define CPM_BACKUP_FILENAME  ".profile.s3bin.backup"

/* S3CP header layout (little-endian). Must match the schema/compiler. */
#define CPM_MAGIC        "S3CP"
#define CPM_VERSION      2
#define CPM_HEADER_SIZE  32
#define CPM_INPUT_ENTRY_SIZE  16
#define CPM_OUTPUT_ENTRY_SIZE 12
#define CPM_MAX_INPUTS        320
#define CPM_MAX_OUTPUTS       160
#define CPM_MAX_PAIR_SLOTS    40
#define CPM_PAIR_SLOT_NONE    0xFF
#define CPM_MAX_RAW_TYPE      7
#define CPM_MAX_OUTPUT_KIND   1

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

#define CPM_PRODUCT_MAX 32

typedef enum {
    CPM_TRANSFER_IDLE = 0,
    CPM_TRANSFER_MATCHED,
    CPM_TRANSFER_TRANSFERRING,
    CPM_TRANSFER_ACTIVE,
    CPM_TRANSFER_FAILED,
    CPM_TRANSFER_UNSUPPORTED,
} controller_profile_transfer_state_t;

typedef struct {
    controller_profile_meta_t profiles[CPM_MAX_PROFILES];
    uint8_t count;
    int8_t matched_index;      /* profile matching the connected VID/PID, -1 = none */
    int8_t active_index;       /* locally active profile, -1 = built-in map */
    controller_profile_transfer_state_t transfer_state;
    bool controller_present;   /* direct USB controller identity is present */
    uint16_t connected_vid;
    uint16_t connected_pid;
    uint16_t connected_caps;   /* CTRL_DESC_CAP_* transport capabilities */
    uint32_t connected_epoch;  /* direct USB connection epoch */
    char connected_product[CPM_PRODUCT_MAX + 1];
} controller_profile_registry_t;

/* ── Firmware entry points (glue; sdkconfig + logging) ──────────────────────── */

/* Reset the singleton registry. */
esp_err_t controller_profile_manager_init(void);

/* Scan CONFIG_CONTROLLER_PROFILE_SD_PATH and log the discovered profiles. */
esp_err_t controller_profile_manager_scan_storage(void);

/* Install a validated profile into the configured SD registry. Runtime rescan
 * is completed and reactivation is queued before this returns. */
esp_err_t controller_profile_manager_install_profile(
    const char *id, const uint8_t *data, size_t len, bool overwrite,
    controller_profile_meta_t *out_meta);

/* Thread-safe registry snapshot for UI / web status. */
esp_err_t controller_profile_manager_get_registry_snapshot(
    controller_profile_registry_t *out_registry);

/* Record a connected controller and select its local profile. */
int controller_profile_manager_on_descriptor(uint16_t vid, uint16_t pid);

/* Full direct USB identity: stores capability bits and product string for
 * UI/web status, then activates a matching local profile. `product` may be
 * NULL. Returns the active index or -1 when the built-in map is used. */
int controller_profile_manager_on_descriptor_report(uint16_t vid, uint16_t pid,
                                                    uint16_t caps,
                                                    const char *product,
                                                    uint32_t connection_epoch);

/* Clear the connected descriptor/profile state and emit one disconnect journal
 * edge. Returns true only when a present controller was actually cleared. */
bool controller_profile_manager_on_disconnect(void);

/* ── Pure helpers (host-testable, no ESP logging) ──────────────────────────── */

/* CRC-32 (IEEE 802.3, zlib-compatible). */
uint32_t controller_profile_crc32(const uint8_t *data, size_t len);

/* Validate an in-memory S3CP blob and fill the format fields of *meta
 * (vid/pid/counts/size and `valid`). Leaves id/path untouched. Returns ESP_OK
 * on a valid profile; ESP_ERR_INVALID_ARG on bad/oversize/corrupt input. */
esp_err_t controller_profile_meta_parse(const uint8_t *data, size_t len,
                                        controller_profile_meta_t *meta);

/* Profile IDs are also directory names. Only ASCII letters, digits, '_' and
 * '-' are accepted so an HTTP-provided ID can never escape `root`. */
bool controller_profile_id_valid(const char *id);

/* Recover a profile directory after an interrupted same-directory swap.
 * A valid target is authoritative; otherwise a backup is restored. Any
 * incomplete upload is discarded. */
esp_err_t controller_profile_storage_recover(const char *root, const char *id);

/* Validate and atomically install a complete S3CP blob as
 * `<root>/<id>/profile.s3bin`. The previous target is retained unless the new
 * file has been fully written, synced, renamed and revalidated. */
esp_err_t controller_profile_storage_install(const char *root, const char *id,
                                             const uint8_t *data, size_t len,
                                             bool overwrite,
                                             controller_profile_meta_t *out_meta);

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

/* Clear only live controller/profile selection state, preserving the scanned
 * profile inventory. Returns true when the registry changed from present. */
bool controller_profile_registry_on_disconnect(controller_profile_registry_t *reg);

/* True when a descriptor can update the current physical-connection binding.
 * Epoch ordering uses serial-number arithmetic so UINT32 wrap is supported. */
bool controller_profile_descriptor_is_fresh(
    const controller_profile_registry_t *reg, uint16_t vid, uint16_t pid,
    uint32_t connection_epoch);

/* Replace only the scanned profile inventory while preserving the connected
 * controller descriptor. A present controller is re-matched and intentionally
 * returned to MATCHED so the new bytes must be activated again. */
void controller_profile_registry_apply_rescan(
    controller_profile_registry_t *registry,
    const controller_profile_registry_t *scanned);

void controller_profile_registry_mark_transfer_started(controller_profile_registry_t *reg,
                                                       int index);

void controller_profile_registry_mark_transfer_active(controller_profile_registry_t *reg,
                                                     int index);

void controller_profile_registry_mark_transfer_failed(controller_profile_registry_t *reg,
                                                     int index);

#ifdef __cplusplus
}
#endif
