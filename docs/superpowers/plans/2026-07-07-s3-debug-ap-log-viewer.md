# S3 Debug AP Log Viewer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a runtime-controlled S3 SoftAP at `192.168.4.1` with a read-only live S3 log viewer, enabled only from a temporary P4 Settings switch.

**Architecture:** P4 owns the settings UI and sends `CTRL_ID_S3_DEBUG_AP` ON/OFF commands over the existing 7-byte `0xA5` control link. S3 owns Wi-Fi SoftAP, HTTP/SSE, and non-blocking log capture, then sends `OFF/STARTING/ON/ERROR` status back to P4 over the same `CTRL_TYPE_STATE` path. The existing P4 Wi-Fi remote stays separate.

**Tech Stack:** ESP-IDF v5.5, C, FreeRTOS queues/tasks, ESP HTTP server, ESP Wi-Fi SoftAP, existing `control_link`, LVGL P4 Settings UI, repo host tests via GCC/PowerShell.

---

## File Structure

Create:

- `firmware/control-board-s3/components/s3_debug_ap/include/s3_debug_ap.h`  
  Public runtime API for OFF/ON requests, status values, optional PC-test hooks, and log-ring access.
- `firmware/control-board-s3/components/s3_debug_ap/s3_debug_log_ring.c`  
  Pure fixed-size log ring buffer, host-testable without ESP-IDF.
- `firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c`  
  Runtime controller: command handling, Wi-Fi/AP lifecycle, HTTP server, SSE endpoint, log hook, status callback.
- `firmware/control-board-s3/components/s3_debug_ap/CMakeLists.txt`  
  ESP-IDF component registration.
- `firmware/control-board-s3/components/s3_debug_ap/Kconfig`  
  Runtime-capable debug AP settings. Default build support enabled, runtime state OFF.
- `tests/s3_debug_ap/test_s3_debug_log_ring.c`  
  Host tests for append/truncate/overflow/snapshot behavior.
- `tests/s3_debug_ap/test_s3_debug_ap_state.c`  
  Host tests for OFF-by-default, start success, stop, and injected start failure.

Modify:

- `firmware/control-board-s3/components/control_link/include/control_link.h`  
  Add `CTRL_ID_S3_DEBUG_AP` and `ctrl_s3_debug_ap_status_t` constants.
- `firmware/main-deck-p4/components/control_link/include/control_link.h`  
  Add matching `CTRL_ID_S3_DEBUG_AP` and status constants.
- `firmware/control-board-s3/components/control_link/control_link_uart.c`  
  Dispatch P4->S3 state command to `s3_debug_ap_request()`.
- `firmware/control-board-s3/main/app_main.c`  
  Initialize S3 debug AP as runtime OFF and register the status callback after `control_link_init()`.
- `firmware/main-deck-p4/components/control_link/control_link_uart.c`  
  Add P4 API for sending state commands to S3.
- `firmware/main-deck-p4/components/control_link/include/control_link.h`  
  Declare `control_link_send_state(uint8_t id, int16_t value)`.
- `firmware/main-deck-p4/components/deck_core/deck_core.c`  
  Consume `CTRL_EV_STATE / CTRL_ID_S3_DEBUG_AP` status frames and expose a callback or state getter for UI.
- `firmware/main-deck-p4/components/ui/include/ui_settings.h`  
  Add callback typedefs and helper functions for the S3 debug AP switch/status.
- `firmware/main-deck-p4/components/ui/ui_settings.c`  
  Add a temporary `S3 DEBUG AP` switch, status label, request callback, and status formatting helper.
- `firmware/main-deck-p4/main/app_main.c`  
  Register the P4 Settings callback that sends S3 debug AP requests. Send OFF at boot after control link init.
- `tests/control_link_protocol/s3_constants.c`
- `tests/control_link_protocol/p4_constants.c`
- `tests/control_link_protocol/test_control_link_protocol.c`
- `tests/run_s3_host_tests.ps1`
- `tests/run_p4_host_tests.ps1`
- `tests/ui_settings/test_ui_settings.c`
- `docs/S3_WIFI_DEBUG_LOG.md`
- `docs/CONTROL_LINK_PROTOCOL.md`
- `docs/ARCHITECTURE.md`
- `docs/STARTUP_CHECKLIST.md`

Do not modify:

- `app_settings` for the new switch. The feature is runtime-only and must not be saved in NVS.
- Existing P4 `wifi_link` or `web_server` behavior except documentation references. The P4 Wi-Fi remote remains separate.

---

## Task 1: Protocol Constants and Status Values

**Files:**

- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `tests/control_link_protocol/s3_constants.c`
- Modify: `tests/control_link_protocol/p4_constants.c`
- Modify: `tests/control_link_protocol/test_control_link_protocol.c`

- [ ] **Step 1: Add failing protocol test prototypes and assertions**

In `tests/control_link_protocol/test_control_link_protocol.c`, add prototypes beside the existing S3/P4 system ID prototypes:

```c
int p4_ctrl_id_s3_debug_ap(void);
int p4_ctrl_s3_debug_ap_off(void);
int p4_ctrl_s3_debug_ap_starting(void);
int p4_ctrl_s3_debug_ap_on(void);
int p4_ctrl_s3_debug_ap_error(void);

int s3_ctrl_id_s3_debug_ap(void);
int s3_ctrl_s3_debug_ap_off(void);
int s3_ctrl_s3_debug_ap_starting(void);
int s3_ctrl_s3_debug_ap_on(void);
int s3_ctrl_s3_debug_ap_error(void);
```

In `test_s3_and_p4_flx4_connection_state_ids_match()` or a new `test_s3_debug_ap_protocol_ids_match()`, add:

```c
assert(s3_ctrl_id_s3_debug_ap() == p4_ctrl_id_s3_debug_ap());
assert(s3_ctrl_id_s3_debug_ap() == CTRL_ID_S3_DEBUG_AP);
assert(s3_ctrl_id_s3_debug_ap() == 0x85);

assert(s3_ctrl_s3_debug_ap_off() == p4_ctrl_s3_debug_ap_off());
assert(s3_ctrl_s3_debug_ap_starting() == p4_ctrl_s3_debug_ap_starting());
assert(s3_ctrl_s3_debug_ap_on() == p4_ctrl_s3_debug_ap_on());
assert(s3_ctrl_s3_debug_ap_error() == p4_ctrl_s3_debug_ap_error());

assert(p4_ctrl_s3_debug_ap_off() == CTRL_S3_DEBUG_AP_OFF);
assert(p4_ctrl_s3_debug_ap_starting() == CTRL_S3_DEBUG_AP_STARTING);
assert(p4_ctrl_s3_debug_ap_on() == CTRL_S3_DEBUG_AP_ON);
assert(p4_ctrl_s3_debug_ap_error() == CTRL_S3_DEBUG_AP_ERROR);
assert(p4_ctrl_s3_debug_ap_off() == 0);
assert(p4_ctrl_s3_debug_ap_starting() == 1);
assert(p4_ctrl_s3_debug_ap_on() == 2);
assert(p4_ctrl_s3_debug_ap_error() == 3);
```

Call the new test from `main()` if it is a separate function.

- [ ] **Step 2: Add failing constant bridge functions**

In `tests/control_link_protocol/s3_constants.c`, add:

```c
int s3_ctrl_id_s3_debug_ap(void) { return CTRL_ID_S3_DEBUG_AP; }
int s3_ctrl_s3_debug_ap_off(void) { return CTRL_S3_DEBUG_AP_OFF; }
int s3_ctrl_s3_debug_ap_starting(void) { return CTRL_S3_DEBUG_AP_STARTING; }
int s3_ctrl_s3_debug_ap_on(void) { return CTRL_S3_DEBUG_AP_ON; }
int s3_ctrl_s3_debug_ap_error(void) { return CTRL_S3_DEBUG_AP_ERROR; }
```

In `tests/control_link_protocol/p4_constants.c`, add:

```c
int p4_ctrl_id_s3_debug_ap(void) { return CTRL_ID_S3_DEBUG_AP; }
int p4_ctrl_s3_debug_ap_off(void) { return CTRL_S3_DEBUG_AP_OFF; }
int p4_ctrl_s3_debug_ap_starting(void) { return CTRL_S3_DEBUG_AP_STARTING; }
int p4_ctrl_s3_debug_ap_on(void) { return CTRL_S3_DEBUG_AP_ON; }
int p4_ctrl_s3_debug_ap_error(void) { return CTRL_S3_DEBUG_AP_ERROR; }
```

- [ ] **Step 3: Run RED test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
```

Expected: `control_link_protocol` build fails because `CTRL_ID_S3_DEBUG_AP` and `CTRL_S3_DEBUG_AP_*` are not defined.

- [ ] **Step 4: Add protocol constants to both headers**

In both S3 and P4 `control_link.h`, below the existing flat overflow IDs:

```c
#define CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT 0x83
#define CTRL_ID_BEAT_FX_BEAT_INC_SHIFT 0x84
#define CTRL_ID_S3_DEBUG_AP            0x85

typedef enum {
    CTRL_S3_DEBUG_AP_OFF = 0,
    CTRL_S3_DEBUG_AP_STARTING = 1,
    CTRL_S3_DEBUG_AP_ON = 2,
    CTRL_S3_DEBUG_AP_ERROR = 3,
} ctrl_s3_debug_ap_status_t;
```

Update the nearby comment to say `0x80..0x82` remain reserved and `0x83..0x85` are current overflow allocations.

- [ ] **Step 5: Run GREEN protocol tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
.\tests\run_p4_host_tests.ps1
```

Expected: both pass. The P4 run must include `control_link_protocol tests passed`.

- [ ] **Step 6: Commit protocol constants**

Run:

```powershell
git add firmware/control-board-s3/components/control_link/include/control_link.h `
        firmware/main-deck-p4/components/control_link/include/control_link.h `
        tests/control_link_protocol/s3_constants.c `
        tests/control_link_protocol/p4_constants.c `
        tests/control_link_protocol/test_control_link_protocol.c
git commit -m "Add S3 debug AP control-link constants"
```

---

## Task 2: S3 Log Ring Buffer

**Files:**

- Create: `firmware/control-board-s3/components/s3_debug_ap/include/s3_debug_ap.h`
- Create: `firmware/control-board-s3/components/s3_debug_ap/s3_debug_log_ring.c`
- Create: `firmware/control-board-s3/components/s3_debug_ap/CMakeLists.txt`
- Create: `tests/s3_debug_ap/test_s3_debug_log_ring.c`
- Modify: `tests/run_s3_host_tests.ps1`

- [ ] **Step 1: Create RED host test**

Create `tests/s3_debug_ap/test_s3_debug_log_ring.c`:

```c
#include "s3_debug_ap.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_empty_snapshot(void)
{
    s3_debug_log_ring_t ring;
    s3_debug_log_ring_init(&ring);

    char out[64];
    size_t n = s3_debug_log_ring_snapshot(&ring, out, sizeof(out), 0);

    assert(n == 0);
    assert(out[0] == '\0');
}

static void test_append_and_snapshot(void)
{
    s3_debug_log_ring_t ring;
    s3_debug_log_ring_init(&ring);

    s3_debug_log_ring_append(&ring, "one\n");
    s3_debug_log_ring_append(&ring, "two\n");

    char out[64];
    size_t n = s3_debug_log_ring_snapshot(&ring, out, sizeof(out), 0);

    assert(n == strlen("one\ntwo\n"));
    assert(strcmp(out, "one\ntwo\n") == 0);
}

static void test_truncates_long_line(void)
{
    s3_debug_log_ring_t ring;
    s3_debug_log_ring_init(&ring);

    char long_line[400];
    memset(long_line, 'A', sizeof(long_line));
    long_line[sizeof(long_line) - 1] = '\0';

    s3_debug_log_ring_append(&ring, long_line);

    char out[512];
    size_t n = s3_debug_log_ring_snapshot(&ring, out, sizeof(out), 0);

    assert(n == S3_DEBUG_LOG_LINE_MAX - 1);
    assert(out[n] == '\0');
}

static void test_overflow_keeps_newest_lines(void)
{
    s3_debug_log_ring_t ring;
    s3_debug_log_ring_init(&ring);

    for (int i = 0; i < S3_DEBUG_LOG_RING_LINES + 4; i++) {
        char line[32];
        snprintf(line, sizeof(line), "line-%02d\n", i);
        s3_debug_log_ring_append(&ring, line);
    }

    char out[2048];
    (void)s3_debug_log_ring_snapshot(&ring, out, sizeof(out), 0);

    assert(strstr(out, "line-00\n") == NULL);
    assert(strstr(out, "line-03\n") == NULL);
    assert(strstr(out, "line-04\n") != NULL);
    assert(strstr(out, "line-19\n") != NULL);
}

int main(void)
{
    test_empty_snapshot();
    test_append_and_snapshot();
    test_truncates_long_line();
    test_overflow_keeps_newest_lines();
    puts("s3_debug_log_ring tests passed");
    return 0;
}
```

- [ ] **Step 2: Add host test to S3 runner**

In `tests/run_s3_host_tests.ps1`, add a `$tests` entry after `control_link_protocol`:

```powershell
@{
    Name = "s3_debug_log_ring"
    Dir = "tests/s3_debug_ap"
    Target = "test_s3_debug_log_ring.exe"
    Args = @(
        "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
        "-DS3_DEBUG_AP_PC_TEST",
        "-I../../firmware/control-board-s3/components/s3_debug_ap/include",
        "-o", "test_s3_debug_log_ring.exe",
        "test_s3_debug_log_ring.c",
        "../../firmware/control-board-s3/components/s3_debug_ap/s3_debug_log_ring.c"
    )
}
```

- [ ] **Step 3: Run RED test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
```

Expected: fails because `s3_debug_ap.h` or `s3_debug_log_ring.c` does not exist.

- [ ] **Step 4: Add public header**

Create `firmware/control-board-s3/components/s3_debug_ap/include/s3_debug_ap.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define S3_DEBUG_AP_SSID "PajoNiiiR-S3-DEBUG"
#define S3_DEBUG_AP_IP "192.168.4.1"
#define S3_DEBUG_LOG_RING_LINES 16
#define S3_DEBUG_LOG_LINE_MAX 256

typedef enum {
    S3_DEBUG_AP_STATUS_OFF = 0,
    S3_DEBUG_AP_STATUS_STARTING = 1,
    S3_DEBUG_AP_STATUS_ON = 2,
    S3_DEBUG_AP_STATUS_ERROR = 3,
} s3_debug_ap_status_t;

typedef void (*s3_debug_ap_status_cb_t)(s3_debug_ap_status_t status);

typedef struct {
    char lines[S3_DEBUG_LOG_RING_LINES][S3_DEBUG_LOG_LINE_MAX];
    uint32_t next_seq;
    uint8_t next_index;
    uint8_t count;
} s3_debug_log_ring_t;

void s3_debug_log_ring_init(s3_debug_log_ring_t *ring);
void s3_debug_log_ring_append(s3_debug_log_ring_t *ring, const char *text);
size_t s3_debug_log_ring_snapshot(const s3_debug_log_ring_t *ring,
                                  char *out,
                                  size_t out_size,
                                  uint32_t after_seq);

esp_err_t s3_debug_ap_init(void);
esp_err_t s3_debug_ap_set_status_callback(s3_debug_ap_status_cb_t cb);
esp_err_t s3_debug_ap_request(bool enable);
s3_debug_ap_status_t s3_debug_ap_status(void);
```

- [ ] **Step 5: Add pure ring implementation**

Create `firmware/control-board-s3/components/s3_debug_ap/s3_debug_log_ring.c`:

```c
#include "s3_debug_ap.h"

#include <stdio.h>
#include <string.h>

void s3_debug_log_ring_init(s3_debug_log_ring_t *ring)
{
    if (!ring) {
        return;
    }
    memset(ring, 0, sizeof(*ring));
}

void s3_debug_log_ring_append(s3_debug_log_ring_t *ring, const char *text)
{
    if (!ring || !text) {
        return;
    }

    snprintf(ring->lines[ring->next_index], S3_DEBUG_LOG_LINE_MAX, "%s", text);
    ring->next_seq++;
    ring->next_index = (uint8_t)((ring->next_index + 1u) % S3_DEBUG_LOG_RING_LINES);
    if (ring->count < S3_DEBUG_LOG_RING_LINES) {
        ring->count++;
    }
}

size_t s3_debug_log_ring_snapshot(const s3_debug_log_ring_t *ring,
                                  char *out,
                                  size_t out_size,
                                  uint32_t after_seq)
{
    if (!ring || !out || out_size == 0) {
        return 0;
    }

    size_t used = 0;
    out[0] = '\0';

    uint32_t first_seq = ring->next_seq - ring->count + 1u;
    for (uint8_t i = 0; i < ring->count; i++) {
        uint32_t seq = first_seq + i;
        if (seq <= after_seq) {
            continue;
        }

        uint8_t index = (uint8_t)((ring->next_index + S3_DEBUG_LOG_RING_LINES -
                                   ring->count + i) % S3_DEBUG_LOG_RING_LINES);
        const char *line = ring->lines[index];
        size_t len = strlen(line);
        if (used + len >= out_size) {
            len = out_size - used - 1u;
        }
        memcpy(out + used, line, len);
        used += len;
        out[used] = '\0';
        if (used + 1u >= out_size) {
            break;
        }
    }
    return used;
}
```

- [ ] **Step 6: Add component CMake**

Create `firmware/control-board-s3/components/s3_debug_ap/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "s3_debug_ap.c" "s3_debug_log_ring.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_http_server esp_netif esp_wifi log nvs_flash
)
```

`s3_debug_ap.c` will be added in Task 3. Until then, ESP-IDF build may fail, but host ring tests should compile directly against `s3_debug_log_ring.c`.

- [ ] **Step 7: Run GREEN ring tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
```

Expected: `s3_debug_log_ring tests passed`.

- [ ] **Step 8: Commit ring buffer**

Run:

```powershell
git add firmware/control-board-s3/components/s3_debug_ap/include/s3_debug_ap.h `
        firmware/control-board-s3/components/s3_debug_ap/s3_debug_log_ring.c `
        firmware/control-board-s3/components/s3_debug_ap/CMakeLists.txt `
        tests/s3_debug_ap/test_s3_debug_log_ring.c `
        tests/run_s3_host_tests.ps1
git commit -m "Add S3 debug AP log ring"
```

---

## Task 3: S3 Runtime State Machine and Control-Link Command Handling

**Files:**

- Create/modify: `firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c`
- Modify: `firmware/control-board-s3/components/control_link/control_link_uart.c`
- Modify: `firmware/control-board-s3/main/app_main.c`
- Create: `tests/s3_debug_ap/test_s3_debug_ap_state.c`
- Modify: `tests/run_s3_host_tests.ps1`

- [ ] **Step 1: Create RED state-machine host test**

Create `tests/s3_debug_ap/test_s3_debug_ap_state.c`:

```c
#include "s3_debug_ap.h"

#include <assert.h>
#include <stdio.h>

static s3_debug_ap_status_t s_seen[8];
static int s_seen_count;

static void status_cb(s3_debug_ap_status_t status)
{
    assert(s_seen_count < (int)(sizeof(s_seen) / sizeof(s_seen[0])));
    s_seen[s_seen_count++] = status;
}

static void reset_seen(void)
{
    s_seen_count = 0;
}

void s3_debug_ap_test_set_start_result(esp_err_t result);
void s3_debug_ap_test_reset(void);

static void test_default_off(void)
{
    s3_debug_ap_test_reset();
    assert(s3_debug_ap_status() == S3_DEBUG_AP_STATUS_OFF);
}

static void test_start_success_reports_starting_then_on(void)
{
    s3_debug_ap_test_reset();
    reset_seen();
    assert(s3_debug_ap_set_status_callback(status_cb) == ESP_OK);
    s3_debug_ap_test_set_start_result(ESP_OK);

    assert(s3_debug_ap_request(true) == ESP_OK);

    assert(s3_debug_ap_status() == S3_DEBUG_AP_STATUS_ON);
    assert(s_seen_count == 2);
    assert(s_seen[0] == S3_DEBUG_AP_STATUS_STARTING);
    assert(s_seen[1] == S3_DEBUG_AP_STATUS_ON);
}

static void test_start_failure_reports_error(void)
{
    s3_debug_ap_test_reset();
    reset_seen();
    assert(s3_debug_ap_set_status_callback(status_cb) == ESP_OK);
    s3_debug_ap_test_set_start_result(ESP_FAIL);

    assert(s3_debug_ap_request(true) == ESP_FAIL);

    assert(s3_debug_ap_status() == S3_DEBUG_AP_STATUS_ERROR);
    assert(s_seen_count == 2);
    assert(s_seen[0] == S3_DEBUG_AP_STATUS_STARTING);
    assert(s_seen[1] == S3_DEBUG_AP_STATUS_ERROR);
}

static void test_stop_reports_off(void)
{
    s3_debug_ap_test_reset();
    reset_seen();
    assert(s3_debug_ap_set_status_callback(status_cb) == ESP_OK);
    s3_debug_ap_test_set_start_result(ESP_OK);
    assert(s3_debug_ap_request(true) == ESP_OK);

    reset_seen();
    assert(s3_debug_ap_request(false) == ESP_OK);

    assert(s3_debug_ap_status() == S3_DEBUG_AP_STATUS_OFF);
    assert(s_seen_count == 1);
    assert(s_seen[0] == S3_DEBUG_AP_STATUS_OFF);
}

int main(void)
{
    test_default_off();
    test_start_success_reports_starting_then_on();
    test_start_failure_reports_error();
    test_stop_reports_off();
    puts("s3_debug_ap_state tests passed");
    return 0;
}
```

- [ ] **Step 2: Add state test to S3 runner**

In `tests/run_s3_host_tests.ps1`, add:

```powershell
@{
    Name = "s3_debug_ap_state"
    Dir = "tests/s3_debug_ap"
    Target = "test_s3_debug_ap_state.exe"
    Args = @(
        "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
        "-DS3_DEBUG_AP_PC_TEST",
        "-I../control_link_protocol/stubs",
        "-I../../firmware/control-board-s3/components/s3_debug_ap/include",
        "-o", "test_s3_debug_ap_state.exe",
        "test_s3_debug_ap_state.c",
        "../../firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c",
        "../../firmware/control-board-s3/components/s3_debug_ap/s3_debug_log_ring.c"
    )
}
```

- [ ] **Step 3: Run RED test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
```

Expected: `s3_debug_ap_state` build fails because `s3_debug_ap.c` does not exist or functions are missing.

- [ ] **Step 4: Add minimal PC-test state machine**

Create `firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c` with a PC-test path first:

```c
#include "s3_debug_ap.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef S3_DEBUG_AP_PC_TEST

static s3_debug_ap_status_t s_status;
static s3_debug_ap_status_cb_t s_status_cb;
static esp_err_t s_test_start_result = ESP_OK;

static void set_status(s3_debug_ap_status_t status)
{
    s_status = status;
    if (s_status_cb) {
        s_status_cb(status);
    }
}

void s3_debug_ap_test_set_start_result(esp_err_t result)
{
    s_test_start_result = result;
}

void s3_debug_ap_test_reset(void)
{
    s_status = S3_DEBUG_AP_STATUS_OFF;
    s_status_cb = NULL;
    s_test_start_result = ESP_OK;
}

esp_err_t s3_debug_ap_init(void)
{
    s_status = S3_DEBUG_AP_STATUS_OFF;
    return ESP_OK;
}

esp_err_t s3_debug_ap_set_status_callback(s3_debug_ap_status_cb_t cb)
{
    s_status_cb = cb;
    return ESP_OK;
}

esp_err_t s3_debug_ap_request(bool enable)
{
    if (!enable) {
        set_status(S3_DEBUG_AP_STATUS_OFF);
        return ESP_OK;
    }

    set_status(S3_DEBUG_AP_STATUS_STARTING);
    if (s_test_start_result != ESP_OK) {
        set_status(S3_DEBUG_AP_STATUS_ERROR);
        return s_test_start_result;
    }
    set_status(S3_DEBUG_AP_STATUS_ON);
    return ESP_OK;
}

s3_debug_ap_status_t s3_debug_ap_status(void)
{
    return s_status;
}

#else
/* ESP-IDF runtime implementation is added in Task 4. */
#endif
```

- [ ] **Step 5: Run GREEN state tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
```

Expected: `s3_debug_ap_state tests passed`.

- [ ] **Step 6: Add S3 control-link dispatch for commands**

In `firmware/control-board-s3/components/control_link/control_link_uart.c`, include the new header:

```c
#include "s3_debug_ap.h"
```

In `handle_p4_frame()`, replace the reserved comment:

```c
    } else if (type == CTRL_TYPE_STATE) {
        if (id == CTRL_ID_S3_DEBUG_AP) {
            (void)s3_debug_ap_request(state != 0);
        }
    }
```

Here `state` is `f[3]`, the low byte of the command value. The high byte is ignored.

- [ ] **Step 7: Add S3 status callback registration**

In `firmware/control-board-s3/main/app_main.c`, include nothing new if `s3_debug_ap.h` is included through another file; otherwise add:

```c
#include "s3_debug_ap.h"
```

Add near the existing helpers:

```c
static void s3_debug_ap_status_cb(s3_debug_ap_status_t status)
{
    (void)control_link_send_semantic(CTRL_TYPE_STATE,
                                     CTRL_ID_S3_DEBUG_AP,
                                     (int16_t)status);
}
```

After `control_link_init(NULL)` in FLX4 translator mode, add:

```c
    ESP_ERROR_CHECK(s3_debug_ap_init());
    ESP_ERROR_CHECK(s3_debug_ap_set_status_callback(s3_debug_ap_status_cb));
```

If raw logger mode also initializes control link in the future, register there too. For current product mode, translator mode is the relevant runtime.

- [ ] **Step 8: Add static guard to S3 runner**

In `tests/run_s3_host_tests.ps1`, add:

```powershell
Assert-FileContains `
    -Name "s3 debug ap control link dispatch" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/control_link/control_link_uart.c") `
    -Patterns @("CTRL_ID_S3_DEBUG_AP", "s3_debug_ap_request")
```

And:

```powershell
Assert-FileContains `
    -Name "s3 debug ap status callback registered after control link" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/main/app_main.c") `
    -Patterns @("s3_debug_ap_init", "s3_debug_ap_set_status_callback", "CTRL_ID_S3_DEBUG_AP")
```

- [ ] **Step 9: Run S3 host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
```

Expected: pass.

- [ ] **Step 10: Commit S3 state/control-link integration**

Run:

```powershell
git add firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c `
        firmware/control-board-s3/components/control_link/control_link_uart.c `
        firmware/control-board-s3/main/app_main.c `
        tests/s3_debug_ap/test_s3_debug_ap_state.c `
        tests/run_s3_host_tests.ps1
git commit -m "Wire S3 debug AP control-link commands"
```

---

## Task 4: S3 SoftAP, HTTP Server, SSE, and Runtime Log Hook

**Files:**

- Modify: `firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c`
- Create: `firmware/control-board-s3/components/s3_debug_ap/Kconfig`
- Modify: `firmware/control-board-s3/components/s3_debug_ap/CMakeLists.txt`
- Modify: `tests/run_s3_host_tests.ps1`

- [ ] **Step 1: Add static guards before implementation**

In `tests/run_s3_host_tests.ps1`, add guards that should fail until runtime code exists:

```powershell
Assert-FileContains `
    -Name "s3 debug ap runtime hosts open softap" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c") `
    -Patterns @("WIFI_MODE_AP", "PajoNiiiR-S3-DEBUG", "192.168.4.1", "esp_wifi_start")

Assert-FileContains `
    -Name "s3 debug ap exposes read only http log page" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c") `
    -Patterns @("httpd_start", "S3 Debug Log", "/events", "text/event-stream")

Assert-FileContains `
    -Name "s3 debug ap log hook remains non blocking" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c") `
    -Patterns @("esp_log_set_vprintf", "s_prev_vprintf", "s3_debug_log_ring_append")
```

- [ ] **Step 2: Run RED static guards**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
```

Expected: fails on one of the new static guards.

- [ ] **Step 3: Add Kconfig**

Create `firmware/control-board-s3/components/s3_debug_ap/Kconfig`:

```kconfig
menu "S3 Debug AP"

config S3_DEBUG_AP_ENABLED
    bool "Build runtime S3 debug AP support"
    default y
    help
        Builds the runtime-controlled S3 debug AP and live log viewer. Runtime
        state remains OFF until P4 sends CTRL_ID_S3_DEBUG_AP ON.

config S3_DEBUG_AP_MAX_CLIENTS
    int "Maximum SoftAP clients"
    default 1
    range 1 4
    depends on S3_DEBUG_AP_ENABLED

endmenu
```

- [ ] **Step 4: Add ESP-IDF runtime implementation**

Replace the `#else` placeholder in `s3_debug_ap.c` with an ESP-IDF implementation following this structure:

```c
#include "sdkconfig.h"

#include <stdarg.h>
#include <stdatomic.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

static const char *TAG = "s3_debug_ap";

static s3_debug_ap_status_t s_status = S3_DEBUG_AP_STATUS_OFF;
static s3_debug_ap_status_cb_t s_status_cb;
static s3_debug_log_ring_t s_log_ring;
static SemaphoreHandle_t s_lock;
static httpd_handle_t s_httpd;
static esp_netif_t *s_ap_netif;
static vprintf_like_t s_prev_vprintf;
static atomic_bool s_log_hook_active;
static bool s_wifi_started;
```

Add these core helpers:

```c
static void set_status(s3_debug_ap_status_t status)
{
    s_status = status;
    if (s_status_cb) {
        s_status_cb(status);
    }
}

static int s3_debug_ap_vprintf(const char *fmt, va_list args)
{
    va_list uart_args;
    va_copy(uart_args, args);
    int ret = s_prev_vprintf ? s_prev_vprintf(fmt, uart_args) : vprintf(fmt, uart_args);
    va_end(uart_args);

    if (atomic_load(&s_log_hook_active)) {
        char line[S3_DEBUG_LOG_LINE_MAX];
        va_list copy_args;
        va_copy(copy_args, args);
        int len = vsnprintf(line, sizeof(line), fmt, copy_args);
        va_end(copy_args);
        if (len > 0 && s_lock && xSemaphoreTake(s_lock, 0) == pdTRUE) {
            s3_debug_log_ring_append(&s_log_ring, line);
            xSemaphoreGive(s_lock);
        }
    }
    return ret;
}
```

Add HTTP handlers:

```c
static esp_err_t root_get_handler(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>S3 Debug Log</title>"
        "<style>body{margin:0;background:#101217;color:#e7eaf0;font:14px monospace;}"
        "header{padding:12px 16px;background:#1c2029;border-bottom:1px solid #333946;}"
        "#log{white-space:pre-wrap;padding:12px 16px;}</style></head>"
        "<body><header><strong>S3 Debug Log</strong><br>"
        "PajoNiiiR-S3-DEBUG / http://192.168.4.1</header><main id=\"log\"></main>"
        "<script>const log=document.getElementById('log');"
        "function start(){const es=new EventSource('/events');"
        "es.onmessage=e=>{log.textContent+=e.data+'\\n';window.scrollTo(0,document.body.scrollHeight);};"
        "es.onerror=()=>{es.close();setTimeout(start,1000);};}start();</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}
```

For `/events`, use a simple bounded loop so one idle browser request cannot live forever:

```c
static esp_err_t events_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char snapshot[2048];
    for (int i = 0; i < 120; i++) {
        snapshot[0] = '\0';
        if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            (void)s3_debug_log_ring_snapshot(&s_log_ring, snapshot, sizeof(snapshot), 0);
            xSemaphoreGive(s_lock);
        }
        if (snapshot[0] != '\0') {
            httpd_resp_sendstr_chunk(req, "data: ");
            for (char *p = snapshot; *p; p++) {
                if (*p == '\n') {
                    httpd_resp_sendstr_chunk(req, "\\ndata: ");
                } else {
                    char c[2] = { *p, '\0' };
                    httpd_resp_sendstr_chunk(req, c);
                }
            }
            httpd_resp_sendstr_chunk(req, "\n\n");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}
```

If this emits repeated snapshots instead of only new lines, accept it for the first batch. A follow-up can add sequence-aware SSE.

Add AP lifecycle:

```c
static esp_err_t start_httpd(void)
{
    if (s_httpd) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &config), TAG, "httpd_start");

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t events = { .uri = "/events", .method = HTTP_GET, .handler = events_get_handler };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &root), TAG, "root handler");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &events), TAG, "events handler");
    return ESP_OK;
}
```

```c
static esp_err_t start_ap(void)
{
    ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "nvs_flash_init");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");
    esp_err_t loop_rc = esp_event_loop_create_default();
    if (loop_rc != ESP_OK && loop_rc != ESP_ERR_INVALID_STATE) {
        return loop_rc;
    }

    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init");

    wifi_config_t ap_config = { 0 };
    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s", S3_DEBUG_AP_SSID);
    ap_config.ap.ssid_len = strlen(S3_DEBUG_AP_SSID);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = CONFIG_S3_DEBUG_AP_MAX_CLIENTS;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "esp_wifi_set_mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "esp_wifi_set_config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start");
    s_wifi_started = true;
    return start_httpd();
}
```

Add stop:

```c
static void stop_ap(void)
{
    atomic_store(&s_log_hook_active, false);
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (s_wifi_started) {
        (void)esp_wifi_stop();
        s_wifi_started = false;
    }
}
```

Public functions:

```c
esp_err_t s3_debug_ap_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "create lock");
    }
    s3_debug_log_ring_init(&s_log_ring);
    set_status(S3_DEBUG_AP_STATUS_OFF);
    return ESP_OK;
}

esp_err_t s3_debug_ap_set_status_callback(s3_debug_ap_status_cb_t cb)
{
    s_status_cb = cb;
    return ESP_OK;
}

esp_err_t s3_debug_ap_request(bool enable)
{
    if (!enable) {
        stop_ap();
        set_status(S3_DEBUG_AP_STATUS_OFF);
        return ESP_OK;
    }

    set_status(S3_DEBUG_AP_STATUS_STARTING);
    if (!s_prev_vprintf) {
        s_prev_vprintf = esp_log_set_vprintf(s3_debug_ap_vprintf);
    }
    atomic_store(&s_log_hook_active, true);

    esp_err_t rc = start_ap();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "debug AP start failed: %s", esp_err_to_name(rc));
        stop_ap();
        set_status(S3_DEBUG_AP_STATUS_ERROR);
        return rc;
    }

    ESP_LOGI(TAG, "S3 debug AP active: SSID=%s URL=http://%s", S3_DEBUG_AP_SSID, S3_DEBUG_AP_IP);
    set_status(S3_DEBUG_AP_STATUS_ON);
    return ESP_OK;
}

s3_debug_ap_status_t s3_debug_ap_status(void)
{
    return s_status;
}
```

- [ ] **Step 5: Update CMake sources and requirements**

Ensure `CMakeLists.txt` includes:

```cmake
idf_component_register(
    SRCS "s3_debug_ap.c" "s3_debug_log_ring.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_http_server esp_netif esp_wifi log nvs_flash
)
```

- [ ] **Step 6: Run S3 host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
```

Expected: pass, including static guards.

- [ ] **Step 7: Build S3 firmware**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py build
```

Expected: build passes. If `nvs_flash_init()` returns already-initialized behavior issues, adjust `start_ap()` to tolerate `ESP_ERR_INVALID_STATE` where ESP-IDF requires it, then rebuild.

- [ ] **Step 8: Commit S3 AP runtime**

Run:

```powershell
git add firmware/control-board-s3/components/s3_debug_ap `
        tests/run_s3_host_tests.ps1
git commit -m "Implement S3 debug AP web log runtime"
```

---

## Task 5: P4 State TX, Status RX, and Settings UI

**Files:**

- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/control_link_uart.c`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `firmware/main-deck-p4/components/ui/include/ui_settings.h`
- Modify: `firmware/main-deck-p4/components/ui/ui_settings.c`
- Modify: `firmware/main-deck-p4/main/app_main.c`
- Modify: `tests/ui_settings/test_ui_settings.c`
- Modify: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Add RED UI helper tests**

In `tests/ui_settings/test_ui_settings.c`, add:

```c
static void test_s3_debug_ap_status_labels(void)
{
    assert(strcmp(ui_settings_s3_debug_ap_status_label(CTRL_S3_DEBUG_AP_OFF), "OFF") == 0);
    assert(strcmp(ui_settings_s3_debug_ap_status_label(CTRL_S3_DEBUG_AP_STARTING), "STARTING") == 0);
    assert(strcmp(ui_settings_s3_debug_ap_status_label(CTRL_S3_DEBUG_AP_ON), "ON") == 0);
    assert(strcmp(ui_settings_s3_debug_ap_status_label(CTRL_S3_DEBUG_AP_ERROR), "ERROR") == 0);
    assert(strcmp(ui_settings_s3_debug_ap_status_label(99), "UNKNOWN") == 0);
}
```

Include `control_link.h` and `string.h`:

```c
#include <string.h>
#include "control_link.h"
```

Call it from `main()`:

```c
    test_s3_debug_ap_status_labels();
```

- [ ] **Step 2: Ensure UI settings test compile includes control_link**

Find the `ui_settings` test entry in `tests/run_p4_host_tests.ps1` and add include path if needed:

```powershell
"-I../../firmware/main-deck-p4/components/control_link/include",
```

- [ ] **Step 3: Run RED P4 host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `ui_settings` build fails because `ui_settings_s3_debug_ap_status_label()` does not exist.

- [ ] **Step 4: Add P4 control-link state TX API**

In `firmware/main-deck-p4/components/control_link/include/control_link.h`, add:

```c
void control_link_send_state(uint8_t id, int16_t value);
```

In `firmware/main-deck-p4/components/control_link/control_link_uart.c`, add:

```c
void control_link_send_state(uint8_t id, int16_t value)
{
    send_frame(CTRL_TYPE_STATE, id, value);
}
```

- [ ] **Step 5: Add UI helper declarations**

In `firmware/main-deck-p4/components/ui/include/ui_settings.h`, add:

```c
const char *ui_settings_s3_debug_ap_status_label(uint8_t status);
```

Inside the non-host-test section, add:

```c
typedef void (*ui_settings_s3_debug_ap_toggle_cb_t)(bool enable);
void ui_settings_set_s3_debug_ap_toggle_cb(ui_settings_s3_debug_ap_toggle_cb_t cb);
void ui_settings_set_s3_debug_ap_status(uint8_t status);
```

- [ ] **Step 6: Implement UI helper and callback storage**

In `firmware/main-deck-p4/components/ui/ui_settings.c`, add near existing Wi-Fi globals:

```c
static ui_settings_s3_debug_ap_toggle_cb_t s_s3_debug_ap_toggle_cb = NULL;
static lv_obj_t *s_label_s3_debug_ap = NULL;
static uint8_t s_s3_debug_ap_status = CTRL_S3_DEBUG_AP_OFF;
```

At top, include:

```c
#include "control_link.h"
```

Add helper outside `#ifndef WIN32` so host tests can call it:

```c
const char *ui_settings_s3_debug_ap_status_label(uint8_t status)
{
    switch (status) {
    case CTRL_S3_DEBUG_AP_OFF: return "OFF";
    case CTRL_S3_DEBUG_AP_STARTING: return "STARTING";
    case CTRL_S3_DEBUG_AP_ON: return "ON";
    case CTRL_S3_DEBUG_AP_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}
```

Add runtime functions:

```c
void ui_settings_set_s3_debug_ap_toggle_cb(ui_settings_s3_debug_ap_toggle_cb_t cb)
{
    s_s3_debug_ap_toggle_cb = cb;
}

static lv_color_t s3_debug_ap_status_color(uint8_t status)
{
    switch (status) {
    case CTRL_S3_DEBUG_AP_ON: return COL_GREEN;
    case CTRL_S3_DEBUG_AP_STARTING: return COL_AMBER;
    case CTRL_S3_DEBUG_AP_ERROR: return COL_RED;
    default: return COL_TEXT_DIM;
    }
}

void ui_settings_set_s3_debug_ap_status(uint8_t status)
{
    s_s3_debug_ap_status = status;
    if (s_label_s3_debug_ap) {
        lv_label_set_text(s_label_s3_debug_ap, ui_settings_s3_debug_ap_status_label(status));
        lv_obj_set_style_text_color(s_label_s3_debug_ap, s3_debug_ap_status_color(status), LV_PART_MAIN);
    }
}
```

- [ ] **Step 7: Add S3 DEBUG AP switch UI**

Add event callback near `wifi_remote_event_cb`:

```c
static void s3_debug_ap_event_cb(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_settings_set_s3_debug_ap_status(on ? CTRL_S3_DEBUG_AP_STARTING : CTRL_S3_DEBUG_AP_OFF);
    if (s_s3_debug_ap_toggle_cb) {
        s_s3_debug_ap_toggle_cb(on);
    }
    ESP_LOGI(TAG, "S3 debug AP request: %s", on ? "on" : "off");
}
```

In `ui_settings_create()`, add a new section below or near `WI-FI REMOTE`. If space is tight, reduce the Wi-Fi remote section height or place S3 debug AP in the status/system area:

```c
    lv_obj_t *s3_debug_section = ui_settings_section(screen, 410, 342, 360, 76, "S3 DEBUG AP");
    lv_obj_t *sw_s3_debug = lv_switch_create(s3_debug_section);
    lv_obj_set_pos(sw_s3_debug, 16, 34);
    lv_obj_add_event_cb(sw_s3_debug, s3_debug_ap_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_label_s3_debug_ap = ui_settings_value_label(s3_debug_section,
                                                  ui_settings_s3_debug_ap_status_label(CTRL_S3_DEBUG_AP_OFF),
                                                  COL_TEXT_DIM,
                                                  &lv_font_montserrat_16,
                                                  104, 34);

    ui_settings_value_label(s3_debug_section,
                            "Open AP: PajoNiiiR-S3-DEBUG / http://192.168.4.1",
                            COL_TEXT_DIM, &lv_font_montserrat_12, 16, 58);
```

If this collides with the existing `MIXER / PFL ROUTING` section at y=346, move `MIXER / PFL ROUTING` down only if there is visible room. If not, shorten the S3 text to `PajoNiiiR-S3-DEBUG / 192.168.4.1` and keep section height 64.

- [ ] **Step 8: Add P4 app callback**

In `firmware/main-deck-p4/main/app_main.c`, add:

```c
static void s3_debug_ap_toggle_cb(bool enable)
{
    control_link_send_state(CTRL_ID_S3_DEBUG_AP, enable ? 1 : 0);
}
```

After existing `ui_settings_set_wifi_toggle_cb(wifi_link_request_enable);`, add:

```c
    ui_settings_set_s3_debug_ap_toggle_cb(s3_debug_ap_toggle_cb);
```

After `control_link_init(...)` succeeds, send safe reset:

```c
    control_link_send_state(CTRL_ID_S3_DEBUG_AP, 0);
```

Place it after control link init, not before.

- [ ] **Step 9: Consume S3 status frames on P4**

In `firmware/main-deck-p4/components/deck_core/deck_core.c`, find the `CTRL_EV_STATE` handling. Add:

```c
if (ev->id == CTRL_ID_S3_DEBUG_AP) {
    ui_settings_set_s3_debug_ap_status((uint8_t)ev->value);
    return;
}
```

If `deck_core.c` cannot include `ui_settings.h` because of component dependency cycles, add a small callback bridge instead:

```c
typedef void (*deck_core_s3_debug_ap_status_cb_t)(uint8_t status);
void deck_core_set_s3_debug_ap_status_cb(deck_core_s3_debug_ap_status_cb_t cb);
```

Then register it from `app_main.c` to call `ui_settings_set_s3_debug_ap_status()`. Prefer the callback bridge if direct include causes a CMake dependency cycle.

- [ ] **Step 10: Add static guards to P4 runner**

In `tests/run_p4_host_tests.ps1`, add:

```powershell
Assert-FileContains `
    -Name "p4 settings exposes s3 debug ap switch" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_settings.c") `
    -Patterns @("S3 DEBUG AP", "PajoNiiiR-S3-DEBUG", "ui_settings_set_s3_debug_ap_status")

Assert-FileContains `
    -Name "p4 sends s3 debug ap control-link command" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/main/app_main.c") `
    -Patterns @("ui_settings_set_s3_debug_ap_toggle_cb", "CTRL_ID_S3_DEBUG_AP", "control_link_send_state")
```

- [ ] **Step 11: Run P4 host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: pass, including `ui_settings tests passed`.

- [ ] **Step 12: Build P4 firmware**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: build passes.

- [ ] **Step 13: Commit P4 UI/control integration**

Run:

```powershell
git add firmware/main-deck-p4/components/control_link/include/control_link.h `
        firmware/main-deck-p4/components/control_link/control_link_uart.c `
        firmware/main-deck-p4/components/deck_core/deck_core.c `
        firmware/main-deck-p4/components/ui/include/ui_settings.h `
        firmware/main-deck-p4/components/ui/ui_settings.c `
        firmware/main-deck-p4/main/app_main.c `
        tests/ui_settings/test_ui_settings.c `
        tests/run_p4_host_tests.ps1
git commit -m "Add P4 Settings control for S3 debug AP"
```

---

## Task 6: Documentation, Full Verification, Flash, and Hardware Smoke

**Files:**

- Modify: `docs/S3_WIFI_DEBUG_LOG.md`
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/STARTUP_CHECKLIST.md`

- [ ] **Step 1: Update S3 Wi-Fi debug documentation**

Replace `docs/S3_WIFI_DEBUG_LOG.md` content with runtime AP instructions:

```markdown
# S3 Debug AP Log Viewer

S3 debug logging is runtime-controlled from the P4 Settings screen. It is OFF
by default and is not persisted across reboot.

1. Open P4 Settings.
2. Enable `S3 DEBUG AP`.
3. Wait for status `ON`.
4. Connect to open Wi-Fi SSID `PajoNiiiR-S3-DEBUG`.
5. Open `http://192.168.4.1`.

The page is read-only and shows live S3 logs. It does not provide remote
commands. Disable `S3 DEBUG AP` when finished.
```

Keep a short note that the old station-mode UDP debug flow has been replaced by the runtime SoftAP flow.

- [ ] **Step 2: Update protocol documentation**

In `docs/CONTROL_LINK_PROTOCOL.md`, add `CTRL_ID_S3_DEBUG_AP` to the system/global ID table:

```markdown
| `0x85` | S3 Debug AP | P4->S3: `0` stop, `1` start; S3->P4 status `0` OFF, `1` STARTING, `2` ON, `3` ERROR |
```

Add a short section:

```markdown
## S3 Debug AP Status

The P4 Settings UI sends `CTRL_TYPE_STATE / CTRL_ID_S3_DEBUG_AP` to request the
S3 runtime debug AP. S3 replies with the same type/id and a status value. The
wire frame length is unchanged.
```

- [ ] **Step 3: Update architecture/startup docs**

In `docs/ARCHITECTURE.md`, under S3 responsibilities, add:

```markdown
- optionally host a runtime-only open debug SoftAP and read-only log viewer
  when P4 explicitly requests it from Settings.
```

Under P4 responsibilities, add:

```markdown
- expose a temporary Settings control for S3 Debug AP and display S3-reported
  OFF/STARTING/ON/ERROR status without persisting the setting.
```

In `docs/STARTUP_CHECKLIST.md`, add a bench smoke item:

```markdown
- S3 Debug AP: default OFF after boot; enabling `S3 DEBUG AP` in P4 Settings
  starts `PajoNiiiR-S3-DEBUG`, serves `http://192.168.4.1`, shows live S3 logs,
  and disabling it removes the AP.
```

- [ ] **Step 4: Run full host tests and diff check**

Run:

```powershell
git diff --check
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
.\tests\run_p4_host_tests.ps1
```

Expected: `git diff --check` has no whitespace errors; S3 and P4 host tests pass.

- [ ] **Step 5: Build both firmware targets**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py build
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: both builds pass.

- [ ] **Step 6: Commit docs and final integration fixes**

Run:

```powershell
git add docs/S3_WIFI_DEBUG_LOG.md docs/CONTROL_LINK_PROTOCOL.md docs/ARCHITECTURE.md docs/STARTUP_CHECKLIST.md
git commit -m "Document runtime S3 debug AP workflow"
```

If Task 4 or Task 5 required small build-fix edits after their commits, include those exact files in this final commit and use:

```powershell
git commit -m "Finalize S3 debug AP integration"
```

- [ ] **Step 7: Flash both devices**

Confirm ports:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name,Description | Format-Table -AutoSize
```

Expected current ports:

```text
COM6  = S3 XIAO / CP210x
COM15 = P4 USB Serial/JTAG
```

Flash:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py -p COM6 flash
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -p COM15 flash
```

- [ ] **Step 8: Hardware smoke**

Run this test with FLX4 connected:

1. Boot both devices.
2. Confirm `PajoNiiiR-S3-DEBUG` is not visible before enabling.
3. On P4 Settings, enable `S3 DEBUG AP`.
4. Confirm P4 status changes `STARTING` -> `ON`.
5. Connect laptop/phone to open SSID `PajoNiiiR-S3-DEBUG`.
6. Open `http://192.168.4.1`.
7. Confirm `S3 Debug Log` page loads and log text updates.
8. While connected, use FLX4 Play/Cue/Browse and confirm MIDI responsiveness remains normal.
9. If FLX4 headphones are connected, confirm audio remains stable.
10. Disable `S3 DEBUG AP`.
11. Confirm P4 status returns `OFF` and AP disappears.

- [ ] **Step 9: Record hardware result**

If smoke passes, update `docs/STARTUP_CHECKLIST.md` S3 Debug AP item with:

```markdown
Hardware smoke passed 2026-07-07: AP defaulted OFF, P4 Settings enabled
`PajoNiiiR-S3-DEBUG`, `http://192.168.4.1` served live S3 logs, FLX4 MIDI/audio
remained responsive, and disabling the switch removed the AP.
```

Commit:

```powershell
git add docs/STARTUP_CHECKLIST.md
git commit -m "Record S3 debug AP smoke result"
```

---

## Final Verification Checklist

- [ ] `git diff --check`
- [ ] `.\tests\run_s3_host_tests.ps1`
- [ ] `.\tests\run_p4_host_tests.ps1`
- [ ] `idf.py build` in `firmware/control-board-s3`
- [ ] `idf.py build` in `firmware/main-deck-p4`
- [ ] S3 flash on COM6 succeeds
- [ ] P4 flash on COM15 succeeds
- [ ] Hardware smoke confirms AP default OFF
- [ ] Hardware smoke confirms P4 status ack `STARTING` -> `ON` -> `OFF`
- [ ] Hardware smoke confirms `http://192.168.4.1` log viewer works
- [ ] Hardware smoke confirms FLX4 MIDI/audio remains responsive while AP is ON

## Self-Review

Spec coverage:

- Runtime-only P4 switch: Task 5.
- Open fixed SSID and `192.168.4.1`: Task 4 and Task 6.
- Read-only live log viewer: Task 4.
- P4->S3 command and S3->P4 ack/status: Tasks 1, 3, and 5.
- Default OFF and no NVS persistence: Tasks 3, 5, and hardware smoke in Task 6.
- Non-blocking log safety: Task 2 and Task 4.
- Tests/build/flash/hardware smoke: Tasks 1-6.

Placeholder scan:

- No `TBD`, `TODO`, or unspecified implementation steps are intentionally left.
- If ESP-IDF API details require minor adjustment during Task 4, the plan says exactly where to adapt and rerun build verification.

Type consistency:

- Wire ID: `CTRL_ID_S3_DEBUG_AP`.
- Wire statuses: `CTRL_S3_DEBUG_AP_OFF`, `CTRL_S3_DEBUG_AP_STARTING`, `CTRL_S3_DEBUG_AP_ON`, `CTRL_S3_DEBUG_AP_ERROR`.
- S3 runtime statuses: `S3_DEBUG_AP_STATUS_OFF`, `S3_DEBUG_AP_STATUS_STARTING`, `S3_DEBUG_AP_STATUS_ON`, `S3_DEBUG_AP_STATUS_ERROR`.
- SSID and URL are consistently `PajoNiiiR-S3-DEBUG` and `http://192.168.4.1`.
