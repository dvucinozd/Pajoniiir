#pragma once

/*
 * P4 microSD structured service-event journal.
 *
 * Producers call service_log_event() from any task; it copies a fixed-size
 * record into a bounded queue without allocating or blocking (a full queue
 * drops and counts, never stalls the caller). One low-priority writer task owns
 * the /sd/logs files, batches writes, rotates four 1 MiB generations and
 * periodically fsyncs. An unavailable or failing microSD is non-fatal.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "service_log_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool      available;       /* writer running and /sd usable */
    uint32_t  queue_depth;     /* records currently queued */
    uint32_t  queue_capacity;  /* SERVICE_LOG_QUEUE_LEN */
    uint32_t  dropped;         /* events dropped on a full queue */
    uint32_t  written;         /* records written to the current file */
    uint64_t  current_bytes;   /* size of the active generation */
    esp_err_t last_error;      /* last write/rotate error */
} service_log_status_t;

/* Start the writer task and emit the boot/schema header. Idempotent. Safe to
 * call before /sd is mounted (records queue and are written once it appears).
 * The boot context (firmware version, running partition, reset reason) is
 * recorded in the per-boot header; any may be NULL. */
esp_err_t service_log_init(const char *fw_version, const char *partition,
                           const char *reset_reason);

/* Enqueue one event (non-blocking, any task). `text` may be NULL; up to
 * `arg_count` (0..4) of a0..a3 are recorded. */
void service_log_event(service_log_event_t event, service_log_severity_t sev,
                       uint8_t arg_count, uint32_t a0, uint32_t a1,
                       uint32_t a2, uint32_t a3, const char *text);

/* Copy a status snapshot for diagnostics / UI / API. */
esp_err_t service_log_get_status(service_log_status_t *out);

/* Flush and fsync the active file now (e.g. before a controlled reboot). */
void service_log_sync(void);

/* Convenience: an event with no numeric args. */
static inline void service_log_note(service_log_event_t event,
                                    service_log_severity_t sev, const char *text)
{
    service_log_event(event, sev, 0u, 0u, 0u, 0u, 0u, text);
}

#ifdef __cplusplus
}
#endif
