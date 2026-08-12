#pragma once

/*
 * Pure formatting/schema core for the P4 microSD service-event journal.
 *
 * Dependency-free: only byte-buffer, integer and string work, so it compiles
 * unchanged on the firmware and the host test target. The FreeRTOS queue, the
 * writer task and the FAT rotation live in the firmware layer on top of this.
 *
 * This journal is a bounded structured event log, NOT a mirror of the ESP_LOG
 * stream: it never records MIDI packets, jog events, audio blocks or per-frame
 * UI/render data.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SERVICE_LOG_SCHEMA_VERSION   1u
#define SERVICE_LOG_TEXT_MAX         48u    /* bounded free-text field         */
#define SERVICE_LOG_LINE_MAX         200u   /* max formatted line incl. NUL    */
#define SERVICE_LOG_QUEUE_LEN        128u   /* fixed producer->writer queue    */
#define SERVICE_LOG_MAX_FILE_BYTES   (1024u * 1024u)  /* 1 MiB per generation  */
#define SERVICE_LOG_GENERATIONS      4u     /* system.log + .1 + .2 + .3        */
#define SERVICE_LOG_ARG_MAX          4u     /* numeric args per event          */

/* Event inventory. X(enum_id, "wire name"). Keep the enum and the name table in
 * lockstep by defining both from this one list. */
#define SERVICE_LOG_EVENTS(X)                          \
    X(SERVICE_LOG_BOOT,               "BOOT")          \
    X(SERVICE_LOG_RESET_REASON,       "RESET_REASON")  \
    X(SERVICE_LOG_FIRMWARE_INFO,      "FIRMWARE_INFO") \
    X(SERVICE_LOG_SD_MOUNTED,         "SD_MOUNTED")    \
    X(SERVICE_LOG_SD_ERROR,           "SD_ERROR")      \
    X(SERVICE_LOG_LOG_OPEN_FAILED,    "LOG_OPEN_FAILED")   \
    X(SERVICE_LOG_LOG_QUEUE_DROPPED,  "LOG_QUEUE_DROPPED") \
    X(SERVICE_LOG_LOW_INTERNAL_HEAP,  "LOW_INTERNAL_HEAP") \
    X(SERVICE_LOG_LOW_PSRAM,          "LOW_PSRAM")     \
    X(SERVICE_LOG_USB_MOUNTED,        "USB_MOUNTED")   \
    X(SERVICE_LOG_USB_UNMOUNTED,      "USB_UNMOUNTED") \
    X(SERVICE_LOG_USB_MOUNT_FAILED,   "USB_MOUNT_FAILED")  \
    X(SERVICE_LOG_LIBRARY_LOADED,     "LIBRARY_LOADED")    \
    X(SERVICE_LOG_LIBRARY_LOAD_FAILED,"LIBRARY_LOAD_FAILED") \
    X(SERVICE_LOG_TRACK_LOAD_START,   "TRACK_LOAD_START")  \
    X(SERVICE_LOG_TRACK_LOAD_DONE,    "TRACK_LOAD_DONE")   \
    X(SERVICE_LOG_TRACK_LOAD_FAILED,  "TRACK_LOAD_FAILED") \
    X(SERVICE_LOG_TRACK_ANLZ_MISSING, "TRACK_ANLZ_MISSING") \
    X(SERVICE_LOG_AUDIO_LOAD_DONE,    "AUDIO_LOAD_DONE")   \
    X(SERVICE_LOG_AUDIO_LOAD_FAILED,  "AUDIO_LOAD_FAILED") \
    X(SERVICE_LOG_AUDIO_DEVICE_ERROR, "AUDIO_DEVICE_ERROR")  \
    X(SERVICE_LOG_AUDIO_UNDERRUN,     "AUDIO_UNDERRUN")    \
    X(SERVICE_LOG_AUDIO_OUTPUT_LATE,  "AUDIO_OUTPUT_LATE") \
    X(SERVICE_LOG_AUDIO_BLOCK_OUTLIER,"AUDIO_BLOCK_OUTLIER") \
    X(SERVICE_LOG_AUDIO_WDT_TRACE,    "AUDIO_WDT_TRACE") \
    X(SERVICE_LOG_LIBRARY_LOAD_TRACE, "LIBRARY_LOAD_TRACE") \
    X(SERVICE_LOG_AUDIO_RING_STARVE,  "AUDIO_RING_STARVATION") \
    X(SERVICE_LOG_AUDIO_RATE_CHANGED, "AUDIO_SAMPLE_RATE_CHANGED") \
    X(SERVICE_LOG_CONTROL_LINK_ONLINE,"CONTROL_LINK_ONLINE")  \
    X(SERVICE_LOG_CONTROL_LINK_OFFLINE,"CONTROL_LINK_OFFLINE") \
    X(SERVICE_LOG_CONTROL_LINK_CRC,   "CONTROL_LINK_CRC_ERROR") \
    X(SERVICE_LOG_CONTROL_LINK_GAP,   "CONTROL_LINK_GAP")  \
    X(SERVICE_LOG_CONTROLLER_CONNECTED,"CONTROLLER_CONNECTED")   \
    X(SERVICE_LOG_CONTROLLER_DISCONNECTED,"CONTROLLER_DISCONNECTED") \
    X(SERVICE_LOG_PROFILE_MATCHED,    "PROFILE_MATCHED")   \
    X(SERVICE_LOG_PROFILE_TRANSFER_DONE,"PROFILE_TRANSFER_DONE") \
    X(SERVICE_LOG_PROFILE_TRANSFER_FAILED,"PROFILE_TRANSFER_FAILED") \
    X(SERVICE_LOG_P4_OTA_STARTED,     "P4_OTA_STARTED")    \
    X(SERVICE_LOG_P4_OTA_VERIFIED,    "P4_OTA_VERIFIED")   \
    X(SERVICE_LOG_P4_OTA_FAILED,      "P4_OTA_FAILED")     \
    X(SERVICE_LOG_S3_OTA_STARTED,     "S3_OTA_STARTED")    \
    X(SERVICE_LOG_S3_OTA_DONE,        "S3_OTA_DONE")       \
    X(SERVICE_LOG_S3_OTA_FAILED,      "S3_OTA_FAILED")     \
    X(SERVICE_LOG_PROFILE_UPLOAD_DONE,"PROFILE_UPLOAD_DONE")   \
    X(SERVICE_LOG_PROFILE_UPLOAD_FAILED,"PROFILE_UPLOAD_FAILED") \
    X(SERVICE_LOG_WEB_LOAD_REQ_FAILED,"WEB_LOAD_REQUEST_FAILED") \
    X(SERVICE_LOG_RECORDING_STARTED,  "RECORDING_STARTED") \
    X(SERVICE_LOG_RECORDING_STOPPED,  "RECORDING_STOPPED") \
    X(SERVICE_LOG_RECORDING_FAILED,   "RECORDING_FAILED")  \
    X(SERVICE_LOG_RECORDING_RECOVERED,"RECORDING_RECOVERED") \
    X(SERVICE_LOG_RECORDING_DROPPED,  "RECORDING_DROPPED") \
    X(SERVICE_LOG_RECORDING_SD_STALL, "RECORDING_SD_STALL") \
    X(SERVICE_LOG_WIFI_ENABLE_REQ,    "WIFI_ENABLE_REQUESTED") \
    X(SERVICE_LOG_WIFI_STARTED,       "WIFI_STARTED") \
    X(SERVICE_LOG_WIFI_FAILED,        "WIFI_FAILED") \
    X(SERVICE_LOG_WIFI_STOPPED,       "WIFI_STOPPED")

typedef enum {
#define X(id, name) id,
    SERVICE_LOG_EVENTS(X)
#undef X
    SERVICE_LOG_EVENT_COUNT
} service_log_event_t;

typedef enum {
    SERVICE_LOG_INFO = 0,
    SERVICE_LOG_WARN,
    SERVICE_LOG_ERROR,
} service_log_severity_t;

typedef struct {
    uint32_t seq;
    uint32_t boot_id;
    uint32_t uptime_ms;
    service_log_event_t   event;
    service_log_severity_t severity;
    uint8_t  arg_count;                 /* number of meaningful args (0..4) */
    uint32_t args[SERVICE_LOG_ARG_MAX];
    char     text[SERVICE_LOG_TEXT_MAX];/* free text; sanitized on format   */
} service_log_record_t;

/* Wire name for an event id ("UNKNOWN" if out of range). */
const char *service_log_event_name(service_log_event_t event);

/* Single-letter severity ('I'/'W'/'E'). */
char service_log_severity_char(service_log_severity_t severity);

/* Copy `src` into `dst` (NUL-terminated, bounded by dst_len), replacing any
 * newline or control byte with '.'. Returns the resulting string length. */
size_t service_log_sanitize(char *dst, size_t dst_len, const char *src);

/* Format one record as a single '\n'-terminated key=value line into `out`.
 * Emits seq/boot/ms/level/event, arg0..arg(arg_count-1) and a sanitized msg
 * when present. Returns the line length (excluding NUL), or 0 on error. */
int service_log_format_record(char *out, size_t out_len,
                              const service_log_record_t *rec);

/* Format the once-per-boot schema/boot header line. */
int service_log_format_header(char *out, size_t out_len, uint32_t boot_id,
                              const char *fw_version, const char *partition,
                              const char *reset_reason);

/* True when a log generation of `current_bytes` has reached `max_bytes`. */
bool service_log_should_rotate(uint64_t current_bytes, uint32_t max_bytes);

#ifdef __cplusplus
}
#endif
