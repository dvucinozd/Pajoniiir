# P4 High/Medium Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the High and Medium findings from the 2026-06-18 code review while keeping Low/dead-code cleanup out of this pass.

**Architecture:** Keep S3/P4 ownership unchanged: S3 remains a MIDI/control-link translator, P4 remains authoritative for state. The web server gets small host-testable helper functions for JSON formatting, loop math, and DNS reply building so firmware behavior can be regression-tested without running `esp_http_server` on host.

**Tech Stack:** ESP-IDF v5.5, C99 host tests through PowerShell runners, ESP32-S3/P4 firmware builds with `idf.py build`.

---

## Scope

Fix these findings:

- High: `/api/library` emits invalid JSON when title/artist contain JSON-special characters.
- High: `/api/load` can start concurrent load workers and race through `s_track_load_worker_result`.
- Medium: Deck 2 load failures can report Deck 1 error text.
- Medium: captive DNS reply builder can write past `tx_buffer`.
- Medium: web `loop_4` uses hardcoded 120 BPM and ignores backend errors.
- Medium: `tests/run_p4_host_tests.ps1` is blocked by stale `ui_library_format_row_text()` test calls.

Out of scope:

- Low `#if 0` / disabled feature blocks in UI/settings/overview.
- Documentation-only cleanup unless tests or build commands require an update.

## File Structure

- Modify `tests/ui_library/test_ui_library.c`: update stale unit tests to the current `ui_library_format_row_text()` signature.
- Create `firmware/main-deck-p4/components/web_server/include/web_api_helpers.h`: host-testable web helper declarations.
- Create `firmware/main-deck-p4/components/web_server/web_api_helpers.c`: JSON escape/item formatting and loop duration math.
- Create `tests/web_api_helpers/test_web_api_helpers.c`: host tests for JSON escaping, library item formatting, and loop math.
- Create `firmware/main-deck-p4/components/web_server/include/dns_reply.h`: pure DNS captive reply builder declaration.
- Create `firmware/main-deck-p4/components/web_server/dns_reply.c`: bounds-checked DNS reply builder.
- Create `tests/dns_reply/test_dns_reply.c`: host tests for valid and malformed DNS packets.
- Modify `firmware/main-deck-p4/components/web_server/CMakeLists.txt`: add new source files.
- Modify `firmware/main-deck-p4/components/web_server/web_server.c`: use helper functions, serialize `/api/load`, and return errors on loop failures.
- Modify `firmware/main-deck-p4/components/web_server/dns_server.c`: call `dns_build_captive_reply()` instead of manually writing DNS response bytes.
- Modify `firmware/main-deck-p4/components/ui/ui_library.c`: add a thread-safe load busy guard for UI and HTTP paths, remove global worker result storage, and read deck-specific audio error status.
- Modify `tests/run_p4_host_tests.ps1`: add `web_api_helpers` and `dns_reply` host tests.

---

### Task 1: Unblock P4 Host Regression Runner

**Files:**

- Modify: `tests/ui_library/test_ui_library.c`

- [ ] **Step 1: Update the stale row-format test call**

Replace the first `ui_library_format_row_text()` call with:

```c
ui_library_format_row_text(&out,
                           "A very long title that should be shortened",
                           "An artist name beyond the compact column",
                           "8A",
                           128,
                           367000);
```

Add this assertion after the artist assertion:

```c
assert(strcmp(out.key, "8A") == 0);
```

- [ ] **Step 2: Update the null-input test call**

Replace:

```c
ui_library_format_row_text(&out, NULL, NULL, 0, 0);
```

with:

```c
ui_library_format_row_text(&out, NULL, NULL, NULL, 0, 0);
```

Add:

```c
assert(strcmp(out.key, "") == 0);
```

- [ ] **Step 3: Run the focused test through the existing runner**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
```

Expected before later tasks: runner must progress past `build ui_library`. It may still fail later if added tests do not exist yet.

---

### Task 2: Add Host-Testable Web API Helpers

**Files:**

- Create: `firmware/main-deck-p4/components/web_server/include/web_api_helpers.h`
- Create: `firmware/main-deck-p4/components/web_server/web_api_helpers.c`
- Create: `tests/web_api_helpers/test_web_api_helpers.c`
- Modify: `firmware/main-deck-p4/components/web_server/CMakeLists.txt`
- Modify: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Create the helper header**

Create `firmware/main-deck-p4/components/web_server/include/web_api_helpers.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t web_api_json_escape(const char *src, char *dst, size_t dst_size);

bool web_api_format_library_item(char *dst,
                                 size_t dst_size,
                                 bool first,
                                 int index,
                                 const char *title,
                                 const char *artist,
                                 uint16_t bpm,
                                 uint32_t duration_ms);

uint32_t web_api_loop_length_ms(uint16_t bpm_centi, uint32_t beats);
```

- [ ] **Step 2: Create helper implementation**

Create `firmware/main-deck-p4/components/web_server/web_api_helpers.c`:

```c
#include "web_api_helpers.h"

#include <stdio.h>
#include <string.h>

#define WEB_API_ESC_TITLE_MAX  (96u * 6u + 1u)
#define WEB_API_ESC_ARTIST_MAX (64u * 6u + 1u)

static void append_char(char *dst, size_t dst_size, size_t *written, char c)
{
    if (dst && dst_size > 0 && *written + 1u < dst_size) {
        dst[*written] = c;
    }
    (*written)++;
}

static void append_text(char *dst, size_t dst_size, size_t *written, const char *text)
{
    for (size_t i = 0; text[i] != '\0'; i++) {
        append_char(dst, dst_size, written, text[i]);
    }
}

size_t web_api_json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t written = 0;
    if (!src) {
        src = "";
    }

    for (size_t i = 0; src[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)src[i];
        switch (ch) {
        case '"':  append_text(dst, dst_size, &written, "\\\""); break;
        case '\\': append_text(dst, dst_size, &written, "\\\\"); break;
        case '\b': append_text(dst, dst_size, &written, "\\b"); break;
        case '\f': append_text(dst, dst_size, &written, "\\f"); break;
        case '\n': append_text(dst, dst_size, &written, "\\n"); break;
        case '\r': append_text(dst, dst_size, &written, "\\r"); break;
        case '\t': append_text(dst, dst_size, &written, "\\t"); break;
        default:
            if (ch < 0x20u) {
                char esc[7];
                snprintf(esc, sizeof(esc), "\\u%04X", (unsigned)ch);
                append_text(dst, dst_size, &written, esc);
            } else {
                append_char(dst, dst_size, &written, (char)ch);
            }
            break;
        }
    }

    if (dst && dst_size > 0) {
        dst[written < dst_size ? written : dst_size - 1u] = '\0';
    }
    return written;
}

bool web_api_format_library_item(char *dst,
                                 size_t dst_size,
                                 bool first,
                                 int index,
                                 const char *title,
                                 const char *artist,
                                 uint16_t bpm,
                                 uint32_t duration_ms)
{
    if (!dst || dst_size == 0) {
        return false;
    }

    char title_esc[WEB_API_ESC_TITLE_MAX];
    char artist_esc[WEB_API_ESC_ARTIST_MAX];
    if (web_api_json_escape(title, title_esc, sizeof(title_esc)) >= sizeof(title_esc) ||
        web_api_json_escape(artist, artist_esc, sizeof(artist_esc)) >= sizeof(artist_esc)) {
        dst[0] = '\0';
        return false;
    }

    int n = snprintf(dst, dst_size,
                     "%s{\"index\":%d,\"title\":\"%s\",\"artist\":\"%s\",\"bpm\":%u,\"duration_ms\":%u}",
                     first ? "" : ",",
                     index,
                     title_esc,
                     artist_esc,
                     (unsigned)bpm,
                     (unsigned)duration_ms);
    return n >= 0 && (size_t)n < dst_size;
}

uint32_t web_api_loop_length_ms(uint16_t bpm_centi, uint32_t beats)
{
    if (bpm_centi == 0 || beats == 0) {
        return 0;
    }
    uint64_t numerator = 6000000ull * (uint64_t)beats;
    return (uint32_t)((numerator + (uint64_t)bpm_centi / 2ull) / (uint64_t)bpm_centi);
}
```

- [ ] **Step 3: Add host tests**

Create `tests/web_api_helpers/test_web_api_helpers.c`:

```c
#include "web_api_helpers.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_json_escape_handles_special_chars(void)
{
    char out[128];
    size_t needed = web_api_json_escape("A \"quote\" \\\\ line\n", out, sizeof(out));
    assert(strcmp(out, "A \\\"quote\\\" \\\\\\\\ line\\n") == 0);
    assert(needed == strlen(out));
}

static void test_json_escape_reports_truncation(void)
{
    char out[8];
    size_t needed = web_api_json_escape("123456789", out, sizeof(out));
    assert(strcmp(out, "1234567") == 0);
    assert(needed == 9);
}

static void test_library_item_formats_valid_json_fragment(void)
{
    char out[1280];
    bool ok = web_api_format_library_item(out, sizeof(out), false, 7,
                                          "A \"quoted\" track",
                                          "Back\\Slash",
                                          12850,
                                          367000);
    assert(ok);
    assert(strcmp(out,
                  ",{\"index\":7,\"title\":\"A \\\"quoted\\\" track\",\"artist\":\"Back\\\\Slash\",\"bpm\":12850,\"duration_ms\":367000}") == 0);
}

static void test_loop_length_uses_centi_bpm(void)
{
    assert(web_api_loop_length_ms(12000, 4) == 2000);
    assert(web_api_loop_length_ms(12800, 4) == 1875);
    assert(web_api_loop_length_ms(0, 4) == 0);
}

int main(void)
{
    test_json_escape_handles_special_chars();
    test_json_escape_reports_truncation();
    test_library_item_formats_valid_json_fragment();
    test_loop_length_uses_centi_bpm();
    puts("web_api_helpers tests passed");
    return 0;
}
```

- [ ] **Step 4: Register new firmware source**

Modify `firmware/main-deck-p4/components/web_server/CMakeLists.txt` so `SRCS` includes `web_api_helpers.c`:

```cmake
idf_component_register(SRCS "web_server.c" "dns_server.c" "web_api_helpers.c" "dns_reply.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "audio_engine" "control_link" "wifi_link" "ui" "media_catalog" "esp_http_server" "deck_core"
                       EMBED_TXTFILES "web/index.html" "web/style.css" "web/app.js")
```

`dns_reply.c` is added in Task 6; this CMake line can be applied now and the file created before running firmware build.

- [ ] **Step 5: Add helper test to P4 runner**

In `tests/run_p4_host_tests.ps1`, add a test entry before `ui_library`:

```powershell
@{
    Name = "web_api_helpers"
    Dir = "tests/web_api_helpers"
    Target = "test_web_api_helpers.exe"
    Args = @(
        "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
        "-I../../firmware/main-deck-p4/components/web_server/include",
        "-o", "test_web_api_helpers.exe",
        "test_web_api_helpers.c",
        "../../firmware/main-deck-p4/components/web_server/web_api_helpers.c"
    )
},
```

- [ ] **Step 6: Run focused helper test**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
```

Expected after Task 2 and before DNS task: `web_api_helpers tests passed`; runner may fail later until `dns_reply.c` exists if CMake was already changed for firmware only. If using the runner before adding DNS, do not add the DNS test entry yet.

---

### Task 3: Fix `/api/library` JSON Formatting

**Files:**

- Modify: `firmware/main-deck-p4/components/web_server/web_server.c`

- [ ] **Step 1: Include the helper header**

Add near the existing includes:

```c
#include "web_api_helpers.h"
```

- [ ] **Step 2: Replace manual JSON item formatting**

In `api_library_handler()`, replace the `char item[256]` / `snprintf()` block with:

```c
char item[1280];
if (!web_api_format_library_item(item, sizeof(item), first, i,
                                 row.title, row.artist, row.bpm, row.duration_ms)) {
    ESP_LOGW(TAG, "library JSON item too large at index=%d", i);
    continue;
}
int item_len = (int)strlen(item);
```

Keep the existing chunk flush logic, but ensure `item_len` is only used after helper success.

- [ ] **Step 3: Run web helper tests**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
```

Expected: `web_api_helpers tests passed`; the previous `ui_library` signature failure must not recur.

---

### Task 4: Serialize Track Load Requests From UI And Web

**Files:**

- Modify: `firmware/main-deck-p4/components/ui/ui_library.c`

- [ ] **Step 1: Add a firmware critical-section guard**

Near `s_track_load_busy`, add:

```c
#ifndef WIN32
static portMUX_TYPE s_track_load_mux = portMUX_INITIALIZER_UNLOCKED;
#endif
```

Add helper functions below `ui_library_deck_index()`:

```c
static bool ui_library_try_begin_track_load(void)
{
#ifndef WIN32
    bool started = false;
    portENTER_CRITICAL(&s_track_load_mux);
    if (!s_track_load_busy) {
        s_track_load_busy = true;
        started = true;
    }
    portEXIT_CRITICAL(&s_track_load_mux);
    return started;
#else
    if (s_track_load_busy) {
        return false;
    }
    s_track_load_busy = true;
    return true;
#endif
}

static void ui_library_finish_track_load(void)
{
#ifndef WIN32
    portENTER_CRITICAL(&s_track_load_mux);
    s_track_load_busy = false;
    portEXIT_CRITICAL(&s_track_load_mux);
#else
    s_track_load_busy = false;
#endif
}
```

- [ ] **Step 2: Remove the global worker result**

Delete:

```c
static ui_track_load_result_t s_track_load_worker_result;
```

In `ui_track_load_worker()`, replace:

```c
ui_track_load_result_t *result = &s_track_load_worker_result;
memset(result, 0, sizeof(*result));
result->index = req.index;
```

with:

```c
ui_track_load_result_t result;
memset(&result, 0, sizeof(result));
result.index = req.index;
```

Then update all `result->` in that function to `result.` and replace:

```c
xQueueOverwrite(s_track_load_result_q, result);
```

with:

```c
xQueueOverwrite(s_track_load_result_q, &result);
```

- [ ] **Step 3: Use the guard in physical UI load path**

At the start of `ui_library_load_selected_deck()`, replace:

```c
if (s_track_load_busy) {
    ui_library_status_hold("LOAD BUSY", COL_AMBER, 1200);
    return;
}
s_track_load_busy = true;
```

with:

```c
if (!ui_library_try_begin_track_load()) {
    ui_library_status_hold("LOAD BUSY", COL_AMBER, 1200);
    return;
}
```

Replace every later `s_track_load_busy = false;` in this function and in `ui_submit_track_load()` failure paths with:

```c
ui_library_finish_track_load();
```

- [ ] **Step 4: Finish load through the guard in polling paths**

In `ui_apply_usb_removed()` and `ui_poll_track_load_result()`, replace each `s_track_load_busy = false;` with:

```c
ui_library_finish_track_load();
```

- [ ] **Step 5: Use the guard in web/API load path**

In `ui_library_load_track_index_for_deck()`, add deck validation and busy guard:

```c
#ifndef WIN32
    if (deck >= DECK_CORE_DECK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (index < 0 || index >= media_catalog_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ui_library_try_begin_track_load()) {
        return ESP_ERR_INVALID_STATE;
    }
    ui_submit_track_load(index, deck);
    return ESP_OK;
#else
```

Remove the old firmware body after adding this.

- [ ] **Step 6: Run P4 host tests**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
```

Expected: no compile error in `ui_library`; existing tests remain passing.

---

### Task 5: Report Deck-Specific Audio Load Errors

**Files:**

- Modify: `firmware/main-deck-p4/components/ui/ui_library.c`

- [ ] **Step 1: Replace compatibility error read in worker**

In `ui_track_load_worker()`, replace:

```c
const char *audio_err = audio_engine_last_error_text();
snprintf(result.status, sizeof(result.status), "%s",
         audio_err && audio_err[0] ? audio_err : "AUDIO ERR");
```

with:

```c
audio_engine_deck_status_t deck_status = {0};
const char *audio_err = NULL;
if (audio_engine_deck_get_status(req.deck, &deck_status) == ESP_OK) {
    audio_err = deck_status.last_error_text;
}
snprintf(result.status, sizeof(result.status), "%s",
         audio_err && audio_err[0] ? audio_err : "AUDIO ERR");
```

- [ ] **Step 2: Run P4 host tests**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
```

Expected: runner reaches and passes existing `audio_engine` and `ui_library` tests. Firmware-only async worker behavior is covered by the P4 build in final verification.

---

### Task 6: Replace Manual DNS Response Writing With Bounds-Checked Builder

**Files:**

- Create: `firmware/main-deck-p4/components/web_server/include/dns_reply.h`
- Create: `firmware/main-deck-p4/components/web_server/dns_reply.c`
- Create: `tests/dns_reply/test_dns_reply.c`
- Modify: `firmware/main-deck-p4/components/web_server/dns_server.c`
- Modify: `tests/run_p4_host_tests.ps1`

- [ ] **Step 1: Create DNS reply header**

Create `firmware/main-deck-p4/components/web_server/include/dns_reply.h`:

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

int dns_build_captive_reply(const uint8_t *query,
                            size_t query_len,
                            uint8_t *reply,
                            size_t reply_cap);
```

- [ ] **Step 2: Create DNS reply implementation**

Create `firmware/main-deck-p4/components/web_server/dns_reply.c`:

```c
#include "dns_reply.h"

#include <string.h>

#define DNS_HEADER_LEN 12u

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static int question_end(const uint8_t *query, size_t query_len)
{
    size_t pos = DNS_HEADER_LEN;
    while (pos < query_len) {
        uint8_t label_len = query[pos];
        if (label_len == 0) {
            pos++;
            return (pos + 4u <= query_len) ? (int)(pos + 4u) : -1;
        }
        if ((label_len & 0xC0u) != 0 || label_len > 63u) {
            return -1;
        }
        if (pos + 1u + label_len > query_len) {
            return -1;
        }
        pos += 1u + label_len;
    }
    return -1;
}

int dns_build_captive_reply(const uint8_t *query,
                            size_t query_len,
                            uint8_t *reply,
                            size_t reply_cap)
{
    if (!query || !reply || query_len < DNS_HEADER_LEN || reply_cap < DNS_HEADER_LEN) {
        return -1;
    }
    uint16_t flags = get_u16(query + 2);
    if ((flags & 0x8000u) != 0) {
        return 0;
    }
    uint16_t qdcount = get_u16(query + 4);
    if (qdcount == 0) {
        return -1;
    }

    int q_end = question_end(query, query_len);
    if (q_end < 0) {
        return -1;
    }

    size_t question_len = (size_t)q_end - DNS_HEADER_LEN;
    size_t needed = DNS_HEADER_LEN + question_len + 16u;
    if (needed > reply_cap) {
        return -1;
    }

    memset(reply, 0, reply_cap);
    reply[0] = query[0];
    reply[1] = query[1];
    put_u16(reply + 2, (uint16_t)(0x8400u | (flags & 0x0100u)));
    put_u16(reply + 4, 1);
    put_u16(reply + 6, 1);
    put_u16(reply + 8, 0);
    put_u16(reply + 10, 0);

    memcpy(reply + DNS_HEADER_LEN, query + DNS_HEADER_LEN, question_len);
    size_t pos = DNS_HEADER_LEN + question_len;

    reply[pos++] = 0xC0u;
    reply[pos++] = 0x0Cu;
    reply[pos++] = 0x00u;
    reply[pos++] = 0x01u;
    reply[pos++] = 0x00u;
    reply[pos++] = 0x01u;
    reply[pos++] = 0x00u;
    reply[pos++] = 0x00u;
    reply[pos++] = 0x00u;
    reply[pos++] = 60u;
    reply[pos++] = 0x00u;
    reply[pos++] = 0x04u;
    reply[pos++] = 192u;
    reply[pos++] = 168u;
    reply[pos++] = 4u;
    reply[pos++] = 1u;

    return (int)pos;
}
```

- [ ] **Step 3: Create DNS host tests**

Create `tests/dns_reply/test_dns_reply.c`:

```c
#include "dns_reply.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_valid_query_gets_captive_a_record(void)
{
    const uint8_t query[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm',
        0x00,
        0x00, 0x01,
        0x00, 0x01
    };
    uint8_t reply[512];
    int len = dns_build_captive_reply(query, sizeof(query), reply, sizeof(reply));
    assert(len > 0);
    assert(reply[0] == 0x12 && reply[1] == 0x34);
    assert(reply[2] == 0x85 && reply[3] == 0x00);
    assert(reply[4] == 0x00 && reply[5] == 0x01);
    assert(reply[6] == 0x00 && reply[7] == 0x01);
    assert(reply[len - 4] == 192);
    assert(reply[len - 3] == 168);
    assert(reply[len - 2] == 4);
    assert(reply[len - 1] == 1);
}

static void test_malformed_label_is_rejected(void)
{
    uint8_t query[20] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x3F
    };
    uint8_t reply[32];
    assert(dns_build_captive_reply(query, sizeof(query), reply, sizeof(reply)) < 0);
}

static void test_tiny_reply_buffer_is_rejected(void)
{
    const uint8_t query[] = {
        0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0,
        0x01, 'a', 0x00, 0x00, 0x01, 0x00, 0x01
    };
    uint8_t reply[16];
    assert(dns_build_captive_reply(query, sizeof(query), reply, sizeof(reply)) < 0);
}

int main(void)
{
    test_valid_query_gets_captive_a_record();
    test_malformed_label_is_rejected();
    test_tiny_reply_buffer_is_rejected();
    puts("dns_reply tests passed");
    return 0;
}
```

- [ ] **Step 4: Use builder in DNS server**

In `dns_server.c`, add:

```c
#include "dns_reply.h"
```

Inside `dns_server_task()`, replace the manual response construction from `dns_header_t *rx_header = ...` through `sendto(...)` with:

```c
int tx_len = dns_build_captive_reply(rx_buffer, (size_t)len, tx_buffer, sizeof(tx_buffer));
if (tx_len > 0) {
    sendto(s_dns_socket, tx_buffer, (size_t)tx_len, 0,
           (struct sockaddr *)&source_addr, socklen);
}
```

After this replacement, remove the now-unused `dns_header_t` and `dns_question_t` typedefs if the compiler reports them unused.

- [ ] **Step 5: Add DNS test to P4 runner**

Add this test entry in `tests/run_p4_host_tests.ps1`:

```powershell
@{
    Name = "dns_reply"
    Dir = "tests/dns_reply"
    Target = "test_dns_reply.exe"
    Args = @(
        "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
        "-I../../firmware/main-deck-p4/components/web_server/include",
        "-o", "test_dns_reply.exe",
        "test_dns_reply.c",
        "../../firmware/main-deck-p4/components/web_server/dns_reply.c"
    )
},
```

- [ ] **Step 6: Run P4 host tests**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
```

Expected: `dns_reply tests passed`.

---

### Task 7: Make Web `loop_4` Use Deck BPM And Return Backend Errors

**Files:**

- Modify: `firmware/main-deck-p4/components/web_server/web_server.c`

- [ ] **Step 1: Replace hardcoded loop math**

In `api_control_handler()`, replace the `loop_4` branch body with:

```c
audio_engine_deck_status_t status = {0};
esp_err_t status_rc = audio_engine_deck_get_status(deck, &status);
if (status_rc != ESP_OK || !status.loaded) {
    httpd_resp_send_err(req, HTTPD_409_CONFLICT, "Deck not loaded");
    return ESP_FAIL;
}

char title[4] = {0};
char artist[4] = {0};
uint16_t bpm_centi = 0;
uint32_t duration_ms = 0;
ui_get_deck_track_info(deck, title, sizeof(title), artist, sizeof(artist),
                       &bpm_centi, &duration_ms);
if (bpm_centi == 0) {
    bpm_centi = 12000;
}

uint32_t loop_len_ms = web_api_loop_length_ms(bpm_centi, 4);
if (loop_len_ms == 0) {
    httpd_resp_send_err(req, HTTPD_409_CONFLICT, "Invalid BPM");
    return ESP_FAIL;
}

esp_err_t loop_rc = audio_engine_deck_set_loop(deck, status.position_ms,
                                               status.position_ms + loop_len_ms);
if (loop_rc != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_409_CONFLICT, "Loop failed");
    return ESP_FAIL;
}
```

Keep the existing `loop_clear` branch, but change it to check return code:

```c
esp_err_t loop_rc = audio_engine_deck_clear_loop(deck);
if (loop_rc != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_409_CONFLICT, "Loop clear failed");
    return ESP_FAIL;
}
```

- [ ] **Step 2: Run web helper tests**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
```

Expected: `web_api_helpers` loop math test remains passing.

---

### Task 8: Final Verification

**Files:**

- No code changes unless verification exposes a failure.

- [ ] **Step 1: Run whitespace check**

Run:

```powershell
git diff --check
```

Expected: exit `0`, no output.

- [ ] **Step 2: Run S3 host tests**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_s3_host_tests.ps1
```

Expected: `S3 host tests passed.`

- [ ] **Step 3: Run P4 host tests**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_p4_host_tests.ps1
```

Expected: `P4 host tests passed.`

- [ ] **Step 4: Build S3 firmware**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py build
```

Expected: `Project build complete.`

- [ ] **Step 5: Build P4 firmware**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: `Project build complete.`

- [ ] **Step 6: Check git status**

Run:

```powershell
git status --short
```

Expected: only intentional source/test/doc changes; no `build/`, `managed_components/`, `sdkconfig`, or `dependencies.lock` staged for commit.

---

## Self-Review

- Spec coverage: all High and Medium review findings are mapped to Tasks 1-7.
- Placeholder scan: no task depends on unspecified behavior; each new helper has concrete signatures and test cases.
- Type consistency: helper names use `web_api_*` consistently; DNS builder uses byte-order helpers internally and does not depend on lwIP for host tests.
- Scope control: Low `#if 0` cleanup is deliberately excluded and should be reviewed separately after these fixes are verified.
