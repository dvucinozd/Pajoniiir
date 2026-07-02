# FLX4 USB Audio Headphones Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Route the existing P4 headphone/cue monitor mix to the physical Pioneer DDJ-FLX4 headphones output over USB Audio Class, while keeping P4 authoritative for audio/mixer state and keeping S3 responsible for the FLX4 USB connection.

**Architecture:** The P4 audio engine already produces a stereo `hp_out` monitor buffer in `ae_output_task()`. The FLX4 headphones jack is not connected to the P4 analog monitor path; it must receive USB Audio playback frames through the same physical USB connection that S3 currently uses for FLX4 MIDI. The preferred implementation keeps FLX4 USB ownership on S3, adds a real-time-safe P4-to-S3 PCM transport for the P4 headphone buffer, and adds an S3 USB Audio Class host streamer that sends those samples to the FLX4 audio playback endpoint or channels discovered from the FLX4 descriptors.

**Tech Stack:** ESP-IDF v5.5, ESP32-P4, ESP32-S3, ESP-IDF USB Host, USB Audio Class descriptor parsing, isochronous USB OUT transfers, existing P4 `audio_engine` / `audio_output_mixer`, existing S3 `flx4_midi_host`, host GCC regression tests, COM3 S3 hardware smoke, COM15 P4 hardware smoke.

---

## Why this is a separate plan

Current firmware already routes FLX4 PFL buttons and Headphones Mix into P4 monitor DSP. That path feeds `hp_out` and currently writes it to the P4 ES8311 monitor/speaker path:

- `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`
- `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`

The DDJ-FLX4 physical headphones jack is different. It is behind the FLX4's own USB audio interface, not behind the P4 ES8311 or PCM5102A wiring. Pioneer/AlphaTheta's public DDJ-FLX4 specification states:

- outputs include `MASTER x 1 (RCA x 1)` and `HEADPHONES x 1 (3.5-mm stereo mini jack)`;
- the controller is class compliant;
- sampling rates are `44.1 kHz / 48 kHz`;
- A/D and D/A conversion supports `16 bits / 24 bits`.

Reference: <https://www.pioneerdj.com/en/news/2022/ddj-flx4-2-channel-dj-controller-for-multiple-dj-applications/>

Implication: this is not a mixer-only change. It is a USB Audio host + real-time PCM transport project.

---

## Non-negotiable constraints

1. P4 remains authoritative for deck state, mixer state, PFL state, Headphones Mix, cue mode, audio decode, EQ, FX, limiter, and monitor PCM generation.
2. S3 remains the FLX4 USB host unless a measured hardware blocker proves S3 cannot stream USB Audio while keeping MIDI responsive.
3. Existing `0xA5` `control_link` remains semantic/control only. Do not stream PCM audio over the existing control frame.
4. Existing MIDI input, MIDI LED output, VU feedback throttling, and reconnect behavior must continue to work.
5. If USB Audio headphones fail, P4 audio output must fail open: PCM5102A MAIN OUT and ES8311 monitor/speaker must continue working.
6. Audio output task on P4 must not block on USB, S3, or inter-board transport backpressure.
7. The first accepted slice is headphone output only. FLX4 RCA master over USB may be used for diagnostic channel mapping, but the product target remains PCM5102A RCA as MAIN OUT and FLX4 headphones as CUE/MONITOR.

---

## Bandwidth and timing budget

Payload rates before framing:

| Format | Payload bytes/sec | Payload bits/sec | USB full-speed 1 ms packet payload |
| --- | ---: | ---: | ---: |
| 44.1 kHz stereo 16-bit | 176,400 | 1.411 Mbit/s | alternating 176/180 bytes |
| 48 kHz stereo 16-bit | 192,000 | 1.536 Mbit/s | 192 bytes |
| 44.1 kHz stereo 24-bit | 264,600 | 2.117 Mbit/s | alternating 264/270 bytes |
| 48 kHz stereo 24-bit | 288,000 | 2.304 Mbit/s | 288 bytes |
| 48 kHz 4-channel 16-bit | 384,000 | 3.072 Mbit/s | 384 bytes |

Practical decision:

- Start with 16-bit stereo or 16-bit 4-channel depending on the FLX4 descriptor.
- Prefer `48 kHz` when FLX4 descriptors expose it because packet scheduling is constant `48 frames/ms`.
- Support `44.1 kHz` only after the packet scheduler has a host test for the `44/45 frames per USB frame` pattern.
- Do not use the existing UART control link for payload PCM. Even 48 kHz stereo 16-bit consumes 1.536 Mbit/s before framing, and it would compete with control, heartbeat, LED, and VU traffic.

---

## Preferred architecture

```text
P4 audio_engine
  decode/rings/resampler/EQ/FX/mixer
        |
        | existing per-block hp_out[] stereo int16
        v
P4 monitor PCM publisher
        |
        | dedicated high-bandwidth P4 -> S3 PCM link
        v
S3 P4 audio link receiver + jitter ring
        |
        | 1 ms USB packet scheduler
        v
S3 FLX4 USB Audio Class OUT streamer
        |
        v
DDJ-FLX4 internal USB audio interface -> physical headphones jack
```

Rejected for first implementation:

- **Streaming PCM over 0xA5 control frames:** not enough margin and would corrupt controller responsiveness.
- **Letting S3 compute cue/headphone mix:** S3 has no decoded audio and must not own mixer state.
- **Making USB Audio replace PCM5102A MAIN OUT:** current external DAC path is already accepted for RCA. This project is for controller headphones.

Alternative fallback architecture if the preferred path fails:

- Move FLX4 USB host ownership to P4 or add a P4-side FLX4 USB host path behind an external USB topology. This is a larger redesign because P4 already uses USB host for media storage and current FLX4 MIDI ownership lives on S3.

---

## File structure

### New S3 component: `flx4_usb_audio`

Create:

- `firmware/control-board-s3/components/flx4_usb_audio/CMakeLists.txt`
- `firmware/control-board-s3/components/flx4_usb_audio/Kconfig`
- `firmware/control-board-s3/components/flx4_usb_audio/include/flx4_usb_audio.h`
- `firmware/control-board-s3/components/flx4_usb_audio/include/flx4_uac_descriptors.h`
- `firmware/control-board-s3/components/flx4_usb_audio/include/flx4_uac_packetizer.h`
- `firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_descriptors.c`
- `firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_packetizer.c`
- `firmware/control-board-s3/components/flx4_usb_audio/flx4_usb_audio.c`

Responsibilities:

- Parse USB Audio Control and Audio Streaming descriptors from the FLX4 active configuration descriptor.
- Discover playback-capable isochronous OUT endpoints and alternate settings.
- Select the safest supported format for first slice: 16-bit, 2-channel or 4-channel, 44.1/48 kHz.
- Build 1 ms USB Audio OUT payloads from a PCM ring.
- Own isochronous OUT transfers to the FLX4 audio interface after the MIDI interface remains claimed and running.
- Expose low-rate diagnostics: selected interface, alt setting, endpoint, sample rate, channels, bit depth, submitted packets, completed packets, skipped packets, underruns, ring fill.

### S3 integration with current USB host

Modify:

- `firmware/control-board-s3/components/flx4_midi_host/include/flx4_midi_host.h`
- `firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c`
- `firmware/control-board-s3/components/flx4_midi_host/CMakeLists.txt`
- `firmware/control-board-s3/main/CMakeLists.txt`
- `firmware/control-board-s3/main/app_main.c`

Responsibilities:

- Keep the current FLX4 USB device lifecycle in one owner.
- Continue claiming the MIDI streaming interface.
- Optionally claim the UAC playback streaming interface when `CONFIG_DDJ_FLX4_USB_AUDIO_HEADPHONES=y`.
- Ensure MIDI IN/OUT transfer callbacks stay lightweight even when audio is enabled.

### New inter-board PCM transport

Create:

- `firmware/main-deck-p4/components/monitor_pcm_link/CMakeLists.txt`
- `firmware/main-deck-p4/components/monitor_pcm_link/Kconfig`
- `firmware/main-deck-p4/components/monitor_pcm_link/include/monitor_pcm_link.h`
- `firmware/main-deck-p4/components/monitor_pcm_link/monitor_pcm_link.c`
- `firmware/control-board-s3/components/p4_audio_link/CMakeLists.txt`
- `firmware/control-board-s3/components/p4_audio_link/Kconfig`
- `firmware/control-board-s3/components/p4_audio_link/include/p4_audio_link.h`
- `firmware/control-board-s3/components/p4_audio_link/p4_audio_link.c`

Responsibilities:

- Transport raw little-endian PCM frames from P4 to S3 on a dedicated high-bandwidth link.
- Preserve sample rate, channel count, sequence numbers, and frame counts.
- Drop or overwrite old monitor PCM under backpressure instead of blocking P4 audio.
- Surface diagnostics for underrun, overrun, sequence gaps, and link rate.

First hardware transport candidate:

- Dedicated I2S stream, P4 as TX master and S3 as RX slave, because the payload
  is already stereo PCM and I2S carries BCLK, WS/LRCK, and DATA with natural
  audio framing.
- P4 candidate pins are fixed for the next bench pass:
  - GPIO32 = I2S BCLK
  - GPIO34 = I2S WS/LRCK
  - GPIO35 = I2S DOUT
  - GPIO49 = optional READY/FLOW/debug, reserved until a measured need exists
- S3 candidate pins for the same bench pass:
  - GPIO15 = I2S BCLK input
  - GPIO16 = I2S WS/LRCK input
  - GPIO17 = I2S DIN
  - GPIO18 = optional READY/FLOW/debug
- GPIO15/GPIO16 are legacy CDJ jog encoder pins and GPIO17/GPIO18 are legacy
  browse encoder pins; this mapping is valid only for the current DDJ-FLX4 USB
  host firmware path where `panel_io` is inactive. This candidate set
  intentionally avoids GPIO36/GPIO37 because repo notes still flag
  GPIO35-GPIO37 as octal-PSRAM-sensitive on N16R8, avoids GPIO45/GPIO46 because
  they are strapping pins, and leaves GPIO48 free for future LED work.
- If S3 cannot receive this I2S stream reliably while also hosting FLX4 USB,
  evaluate dedicated SPI as the second option. Do not use the existing `0xA5`
  control UART for PCM payload.

### P4 audio engine integration

Modify:

- `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
- `firmware/main-deck-p4/components/audio_engine/CMakeLists.txt`
- `firmware/main-deck-p4/components/web_server/web_server.c`

Responsibilities:

- Publish `hp_out` blocks to `monitor_pcm_link_write()` after the current `audio_output_mixer_next_full()` block is assembled.
- Keep `monitor_pcm_link_write()` non-blocking.
- Add USB headphone diagnostics to `audio_engine_diagnostics_snapshot_t` or expose them separately if they originate on S3.
- Keep ES8311 and PCM5102A writes unchanged.

### Tests

Create:

- `tests/flx4_usb_audio/test_flx4_uac_descriptors.c`
- `tests/flx4_usb_audio/test_flx4_uac_packetizer.c`
- `tests/flx4_usb_audio/Makefile`
- `tests/p4_audio_link/test_p4_audio_link.c`
- `tests/p4_audio_link/Makefile`
- `tests/monitor_pcm_link/test_monitor_pcm_link.c`
- `tests/monitor_pcm_link/Makefile`

Modify:

- `tests/run_s3_host_tests.ps1`
- `tests/run_p4_host_tests.ps1`
- `tests/audio_engine/test_audio_engine.c`

Responsibilities:

- Descriptor parser coverage.
- Packet scheduler coverage for 48 kHz and 44.1 kHz.
- Ring-buffer underrun/overrun behavior.
- P4 non-blocking monitor sink behavior.
- Regression that P4 mixer still produces correct `hp_out` for PFL and Headphones Mix.

### Documentation

Modify:

- `README.md`
- `docs/ARCHITECTURE.md`
- `docs/HARDWARE_WIRING.md`
- `docs/CONTROL_LINK_PROTOCOL.md`
- `docs/DDJ_FLX4_MIDI_MAP.md`
- `docs/DEVELOPMENT_PLAN.md`
- `docs/STARTUP_CHECKLIST.md`
- `docs/RISK_REGISTER.md`

Responsibilities:

- Document that FLX4 headphones are USB Audio, not analog P4 monitor.
- Document chosen inter-board PCM transport pins after bench selection.
- Document hardware smoke checklist.
- Keep `0xA5` as semantic-only; document that PCM audio has a separate transport.

---

## Task 1: FLX4 descriptor capture and parser fixture

**Files:**

- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/include/flx4_midi_host.h`
- Create: `docs/validation/FLX4_USB_AUDIO_DESCRIPTOR_CAPTURE.md`
- Create: `tests/flx4_usb_audio/fixtures/flx4_config_descriptor.bin`

- [x] **Step 1: Add a descriptor dump mode behind a Kconfig flag**

Add to `firmware/control-board-s3/components/flx4_midi_host/Kconfig`:

```kconfig
config DDJ_FLX4_DUMP_USB_CONFIG_DESCRIPTOR
    bool "Dump DDJ-FLX4 active USB configuration descriptor"
    default n
    depends on DDJ_FLX4_HOST_MODE
    help
        Logs the raw active USB configuration descriptor in hex during FLX4
        attach. Use only during descriptor capture because the output is noisy.
```

- [x] **Step 2: Log the active config descriptor in deterministic 16-byte rows**

Add a helper near the other descriptor helpers in `flx4_midi_host.c`:

```c
static void log_config_descriptor_hex(const uint8_t *data, size_t len)
{
#if CONFIG_DDJ_FLX4_DUMP_USB_CONFIG_DESCRIPTOR
    if (!data || len == 0u) {
        ESP_LOGW(TAG, "FLX4 config descriptor: empty");
        return;
    }
    for (size_t offset = 0; offset < len; offset += 16u) {
        char line[16u * 3u + 1u];
        size_t pos = 0u;
        size_t chunk = len - offset;
        if (chunk > 16u) chunk = 16u;
        for (size_t i = 0; i < chunk && pos + 3u < sizeof(line); ++i) {
            int written = snprintf(&line[pos], sizeof(line) - pos, "%02X ", data[offset + i]);
            if (written <= 0) break;
            pos += (size_t)written;
        }
        if (pos > 0u && pos < sizeof(line)) {
            line[pos - 1u] = '\0';
        } else {
            line[sizeof(line) - 1u] = '\0';
        }
        ESP_LOGI(TAG, "FLX4_CFG %04u: %s", (unsigned)offset, line);
    }
#else
    (void)data;
    (void)len;
#endif
}
```

Call it immediately after `usb_host_get_active_config_descriptor()` succeeds:

```c
log_config_descriptor_hex((const uint8_t *)cfg, cfg->wTotalLength);
```

- [x] **Step 3: Build and flash S3 descriptor build**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults" build
```

Expected:

- Build exits `0`.
- No warning becomes an error.

Flash only when the FLX4 is connected to S3 and COM3 is available:

```powershell
idf.py -p COM3 flash monitor
```

Expected:

- FLX4 enumerates as before.
- `FLX4_CFG` rows appear.
- MIDI input still logs or translates normally after descriptor dump.

- [x] **Step 4: Save descriptor fixture**

Convert the `FLX4_CFG` rows from the monitor output into binary:

```powershell
python - <<'PY'
from pathlib import Path
import re
log = Path("D:/Documents/DDJ-FFL4/logs/flx4_descriptor_capture.log").read_text(encoding="utf-8", errors="ignore")
data = bytearray()
for line in log.splitlines():
    m = re.search(r"FLX4_CFG\s+[0-9]+:\s+([0-9A-Fa-f ]+)", line)
    if m:
        data.extend(int(x, 16) for x in m.group(1).split())
out = Path("D:/Documents/DDJ-FFL4/tests/flx4_usb_audio/fixtures/flx4_config_descriptor.bin")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(data)
print(f"wrote {len(data)} bytes")
PY
```

Expected:

- The printed byte count equals the descriptor's `wTotalLength`.
- The first two bytes are `09 02`.

- [x] **Step 5: Document the descriptor findings**

Create `docs/validation/FLX4_USB_AUDIO_DESCRIPTOR_CAPTURE.md` with this structure filled from the actual capture:

```markdown
# FLX4 USB Audio Descriptor Capture

Date: capture date from the COM3 monitor session
Controller: Pioneer DDJ-FLX4
S3 port: COM3
Firmware config: `CONFIG_DDJ_FLX4_DUMP_USB_CONFIG_DESCRIPTOR=y`

## Summary

The FLX4 active configuration descriptor was captured from the S3 USB host.
MIDI interface claiming still works after descriptor dump.

## Interfaces

| Interface | Alternate | Class | Subclass | Protocol | Endpoints | Notes |
| ---: | ---: | ---: | ---: | ---: | --- | --- |
| 4 | 0 | Audio | MIDIStreaming | 0 | bulk/intr IN + OUT | Existing MIDI path |

## Audio streaming candidates

| Interface | Alternate | Direction | Endpoint | Channels | Bits | Sample rates | Max packet | Accepted for first slice |
| ---: | ---: | --- | --- | ---: | ---: | --- | ---: | --- |
| captured value | captured value | OUT | captured value | captured value | captured value | captured value | captured value | yes/no |

## Decision

The first USB Audio implementation will target the accepted row above. If the
accepted row is 4-channel, channels 1/2 carry master for FLX4 RCA diagnostics
and channels 3/4 carry P4 `hp_out` for FLX4 headphones. If the accepted row is
2-channel headphones-only, it carries P4 `hp_out`.
```

Commit after this task:

```powershell
git add firmware/control-board-s3/components/flx4_midi_host docs/validation/FLX4_USB_AUDIO_DESCRIPTOR_CAPTURE.md tests/flx4_usb_audio/fixtures/flx4_config_descriptor.bin
git commit -m "test: capture flx4 usb audio descriptors"
```

---

## Task 2: S3 USB Audio descriptor parser

**Files:**

- Create: `firmware/control-board-s3/components/flx4_usb_audio/include/flx4_uac_descriptors.h`
- Create: `firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_descriptors.c`
- Create: `firmware/control-board-s3/components/flx4_usb_audio/CMakeLists.txt`
- Create: `tests/flx4_usb_audio/test_flx4_uac_descriptors.c`
- Create: `tests/flx4_usb_audio/Makefile`
- Modify: `tests/run_s3_host_tests.ps1`

- [x] **Step 1: Write the descriptor parser API**

Create `flx4_uac_descriptors.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t interface_num;
    uint8_t alternate_setting;
    uint8_t endpoint_addr;
    uint16_t max_packet_size;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint8_t bytes_per_sample;
    uint32_t sample_rates[8];
    uint8_t sample_rate_count;
} flx4_uac_playback_format_t;

typedef struct {
    flx4_uac_playback_format_t formats[8];
    uint8_t format_count;
} flx4_uac_descriptor_result_t;

bool flx4_uac_parse_playback_formats(const uint8_t *config_desc,
                                     size_t config_len,
                                     flx4_uac_descriptor_result_t *out);

bool flx4_uac_select_preferred_format(const flx4_uac_descriptor_result_t *result,
                                      flx4_uac_playback_format_t *out);
```

- [x] **Step 2: Write failing parser tests**

Create `tests/flx4_usb_audio/test_flx4_uac_descriptors.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flx4_uac_descriptors.h"

#define EXPECT_TRUE(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)
#define EXPECT_EQ_U32(a, b, msg) do { if ((uint32_t)(a) != (uint32_t)(b)) { fprintf(stderr, "FAIL: %s got=%u expected=%u\n", msg, (unsigned)(a), (unsigned)(b)); return 1; } } while (0)

static uint8_t *read_fixture(size_t *out_len)
{
    const char *path = "tests/flx4_usb_audio/fixtures/flx4_config_descriptor.bin";
    FILE *f = fopen(path, "rb");
    EXPECT_TRUE(f != NULL, "fixture opens");
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    EXPECT_TRUE(len > 9, "fixture has descriptor bytes");
    uint8_t *data = malloc((size_t)len);
    EXPECT_TRUE(data != NULL, "fixture allocation");
    EXPECT_EQ_U32(fread(data, 1, (size_t)len, f), (uint32_t)len, "fixture read");
    fclose(f);
    *out_len = (size_t)len;
    return data;
}

static int test_parse_real_flx4_descriptor(void)
{
    size_t len = 0;
    uint8_t *data = read_fixture(&len);
    flx4_uac_descriptor_result_t result = { 0 };
    EXPECT_TRUE(flx4_uac_parse_playback_formats(data, len, &result), "parse returns true");
    EXPECT_TRUE(result.format_count > 0, "at least one playback format found");

    bool has_44_or_48 = false;
    for (uint8_t i = 0; i < result.format_count; ++i) {
        const flx4_uac_playback_format_t *fmt = &result.formats[i];
        EXPECT_TRUE((fmt->endpoint_addr & 0x80u) == 0u, "playback endpoint is OUT");
        EXPECT_TRUE(fmt->channels == 2u || fmt->channels == 4u, "channels are stereo or 4-channel");
        EXPECT_TRUE(fmt->bits_per_sample == 16u || fmt->bits_per_sample == 24u, "bits are 16 or 24");
        for (uint8_t s = 0; s < fmt->sample_rate_count; ++s) {
            if (fmt->sample_rates[s] == 44100u || fmt->sample_rates[s] == 48000u) {
                has_44_or_48 = true;
            }
        }
    }
    EXPECT_TRUE(has_44_or_48, "format includes 44.1 or 48 kHz");
    free(data);
    return 0;
}

static int test_select_preferred_format(void)
{
    flx4_uac_descriptor_result_t result = {
        .formats = {
            { .interface_num = 1, .alternate_setting = 1, .endpoint_addr = 0x01, .max_packet_size = 192, .channels = 2, .bits_per_sample = 16, .bytes_per_sample = 2, .sample_rates = { 44100 }, .sample_rate_count = 1 },
            { .interface_num = 1, .alternate_setting = 2, .endpoint_addr = 0x01, .max_packet_size = 192, .channels = 2, .bits_per_sample = 16, .bytes_per_sample = 2, .sample_rates = { 48000 }, .sample_rate_count = 1 },
            { .interface_num = 1, .alternate_setting = 3, .endpoint_addr = 0x01, .max_packet_size = 288, .channels = 2, .bits_per_sample = 24, .bytes_per_sample = 3, .sample_rates = { 48000 }, .sample_rate_count = 1 },
        },
        .format_count = 3,
    };
    flx4_uac_playback_format_t selected = { 0 };
    EXPECT_TRUE(flx4_uac_select_preferred_format(&result, &selected), "select succeeds");
    EXPECT_EQ_U32(selected.sample_rates[0], 48000u, "prefers 48 kHz");
    EXPECT_EQ_U32(selected.bits_per_sample, 16u, "prefers 16-bit first slice");
    EXPECT_EQ_U32(selected.channels, 2u, "prefers stereo over wider output for first slice");
    return 0;
}

int main(void)
{
    if (test_parse_real_flx4_descriptor() != 0) return 1;
    if (test_select_preferred_format() != 0) return 1;
    puts("flx4_uac_descriptors: PASS");
    return 0;
}
```

- [x] **Step 3: Implement the parser**

Implement `flx4_uac_descriptors.c` with these parsing rules:

```c
#include "flx4_uac_descriptors.h"
#include <string.h>

#define USB_DESC_TYPE_CONFIG      0x02u
#define USB_DESC_TYPE_INTERFACE   0x04u
#define USB_DESC_TYPE_ENDPOINT    0x05u
#define USB_DESC_TYPE_CS_INTERFACE 0x24u
#define USB_CLASS_AUDIO           0x01u
#define USB_SUBCLASS_AUDIOSTREAMING 0x02u
#define USB_EP_DIR_IN             0x80u
#define USB_EP_XFER_MASK          0x03u
#define USB_EP_XFER_ISOC          0x01u

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd24(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }

static bool add_rate(flx4_uac_playback_format_t *fmt, uint32_t rate)
{
    if (!fmt || rate == 0u || fmt->sample_rate_count >= 8u) return false;
    fmt->sample_rates[fmt->sample_rate_count++] = rate;
    return true;
}

static bool format_has_rate(const flx4_uac_playback_format_t *fmt, uint32_t rate)
{
    if (!fmt) return false;
    for (uint8_t i = 0; i < fmt->sample_rate_count; ++i) {
        if (fmt->sample_rates[i] == rate) return true;
    }
    return false;
}

bool flx4_uac_parse_playback_formats(const uint8_t *config_desc,
                                     size_t config_len,
                                     flx4_uac_descriptor_result_t *out)
{
    if (!config_desc || config_len < 9u || !out || config_desc[1] != USB_DESC_TYPE_CONFIG) return false;
    memset(out, 0, sizeof(*out));

    size_t total_len = (size_t)config_desc[2] | ((size_t)config_desc[3] << 8);
    if (total_len == 0u || total_len > config_len) total_len = config_len;

    flx4_uac_playback_format_t current = { 0 };
    bool in_as = false;
    bool current_valid = false;

    for (size_t off = config_desc[0]; off + 2u <= total_len; ) {
        uint8_t len = config_desc[off];
        uint8_t type = config_desc[off + 1u];
        if (len < 2u || off + len > total_len) return false;

        if (type == USB_DESC_TYPE_INTERFACE && len >= 9u) {
            if (current_valid && out->format_count < 8u) {
                out->formats[out->format_count++] = current;
            }
            memset(&current, 0, sizeof(current));
            in_as = config_desc[off + 5u] == USB_CLASS_AUDIO &&
                    config_desc[off + 6u] == USB_SUBCLASS_AUDIOSTREAMING &&
                    config_desc[off + 3u] != 0u;
            current_valid = in_as;
            if (in_as) {
                current.interface_num = config_desc[off + 2u];
                current.alternate_setting = config_desc[off + 3u];
            }
        } else if (in_as && type == USB_DESC_TYPE_CS_INTERFACE && len >= 8u) {
            const uint8_t subtype = config_desc[off + 2u];
            if (subtype == 0x02u && len >= 8u) {
                current.channels = config_desc[off + 4u];
            } else if (subtype == 0x03u && len >= 8u) {
                current.bytes_per_sample = config_desc[off + 5u];
                current.bits_per_sample = config_desc[off + 6u];
                uint8_t nr = config_desc[off + 7u];
                if (nr > 0u && len >= (uint8_t)(8u + (3u * nr))) {
                    for (uint8_t i = 0u; i < nr; ++i) {
                        add_rate(&current, rd24(&config_desc[off + 8u + (3u * i)]));
                    }
                }
            }
        } else if (in_as && type == USB_DESC_TYPE_ENDPOINT && len >= 7u) {
            uint8_t ep = config_desc[off + 2u];
            uint8_t attr = config_desc[off + 3u] & USB_EP_XFER_MASK;
            if ((ep & USB_EP_DIR_IN) == 0u && attr == USB_EP_XFER_ISOC) {
                current.endpoint_addr = ep;
                current.max_packet_size = rd16(&config_desc[off + 4u]);
            }
        }
        off += len;
    }

    if (current_valid && out->format_count < 8u) {
        out->formats[out->format_count++] = current;
    }

    return out->format_count > 0u;
}

bool flx4_uac_select_preferred_format(const flx4_uac_descriptor_result_t *result,
                                      flx4_uac_playback_format_t *out)
{
    if (!result || !out) return false;
    const flx4_uac_playback_format_t *best = 0;
    int best_score = -1;
    for (uint8_t i = 0; i < result->format_count; ++i) {
        const flx4_uac_playback_format_t *fmt = &result->formats[i];
        if (fmt->endpoint_addr == 0u || fmt->channels == 0u || fmt->bits_per_sample == 0u) continue;
        int score = 0;
        if (format_has_rate(fmt, 48000u)) score += 100;
        if (format_has_rate(fmt, 44100u)) score += 50;
        if (fmt->bits_per_sample == 16u) score += 30;
        if (fmt->channels == 2u) score += 20;
        if (fmt->channels == 4u) score += 10;
        if (score > best_score) {
            best_score = score;
            best = fmt;
        }
    }
    if (!best) return false;
    *out = *best;
    return true;
}
```

- [x] **Step 4: Add Makefile and test runner entry**

Create `tests/flx4_usb_audio/Makefile`:

```make
CC ?= gcc
CFLAGS += -std=c99 -Wall -Wextra -Werror
CFLAGS += -I../../firmware/control-board-s3/components/flx4_usb_audio/include

all: test_flx4_uac_descriptors
	./test_flx4_uac_descriptors

test_flx4_uac_descriptors: test_flx4_uac_descriptors.c ../../firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_descriptors.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f test_flx4_uac_descriptors
```

Add to `tests/run_s3_host_tests.ps1`:

```powershell
@{ Name = "flx4_usb_audio"; Path = "tests\flx4_usb_audio" }
```

- [x] **Step 5: Run tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cd D:\Documents\DDJ-FFL4
.\tests\run_s3_host_tests.ps1
```

Expected:

- `flx4_uac_descriptors: PASS`
- Existing S3 tests still pass.

Commit:

```powershell
git add firmware/control-board-s3/components/flx4_usb_audio tests/flx4_usb_audio tests/run_s3_host_tests.ps1
git commit -m "test: add flx4 usb audio descriptor parser"
```

---

## Task 3: USB Audio packet scheduler

**Files:**

- Create: `firmware/control-board-s3/components/flx4_usb_audio/include/flx4_uac_packetizer.h`
- Create: `firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_packetizer.c`
- Create: `tests/flx4_usb_audio/test_flx4_uac_packetizer.c`
- Modify: `tests/flx4_usb_audio/Makefile`

- [x] **Step 1: Add packetizer API**

Create `flx4_uac_packetizer.h`:

```c
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bytes_per_sample;
    uint32_t frame_accum;
} flx4_uac_packetizer_t;

void flx4_uac_packetizer_init(flx4_uac_packetizer_t *p,
                              uint32_t sample_rate,
                              uint8_t channels,
                              uint8_t bytes_per_sample);

uint16_t flx4_uac_packetizer_next_frames(flx4_uac_packetizer_t *p);
size_t flx4_uac_packetizer_next_bytes(flx4_uac_packetizer_t *p);
```

- [x] **Step 2: Add failing tests for 48 kHz and 44.1 kHz**

Create `test_flx4_uac_packetizer.c`:

```c
#include <stdio.h>
#include "flx4_uac_packetizer.h"

#define EXPECT_EQ(a,b,msg) do { if ((a)!=(b)) { fprintf(stderr, "FAIL: %s got=%u expected=%u\n", msg, (unsigned)(a), (unsigned)(b)); return 1; } } while (0)

static int test_48k_stereo_16_is_constant(void)
{
    flx4_uac_packetizer_t p;
    flx4_uac_packetizer_init(&p, 48000u, 2u, 2u);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(flx4_uac_packetizer_next_frames(&p), 48u, "48k frames per ms");
    }
    return 0;
}

static int test_44k1_accumulates_44100_frames_per_second(void)
{
    flx4_uac_packetizer_t p;
    flx4_uac_packetizer_init(&p, 44100u, 2u, 2u);
    uint32_t total = 0;
    uint32_t packets_44 = 0;
    uint32_t packets_45 = 0;
    for (int i = 0; i < 1000; ++i) {
        uint16_t frames = flx4_uac_packetizer_next_frames(&p);
        total += frames;
        if (frames == 44u) packets_44++;
        if (frames == 45u) packets_45++;
    }
    EXPECT_EQ(total, 44100u, "44.1k total frames per 1000 USB frames");
    EXPECT_EQ(packets_44, 900u, "44-frame packets");
    EXPECT_EQ(packets_45, 100u, "45-frame packets");
    return 0;
}

static int test_4ch_16_byte_count(void)
{
    flx4_uac_packetizer_t p;
    flx4_uac_packetizer_init(&p, 48000u, 4u, 2u);
    EXPECT_EQ(flx4_uac_packetizer_next_bytes(&p), 384u, "48k 4ch 16-bit bytes per ms");
    return 0;
}

int main(void)
{
    if (test_48k_stereo_16_is_constant() != 0) return 1;
    if (test_44k1_accumulates_44100_frames_per_second() != 0) return 1;
    if (test_4ch_16_byte_count() != 0) return 1;
    puts("flx4_uac_packetizer: PASS");
    return 0;
}
```

- [x] **Step 3: Implement packetizer**

Create `flx4_uac_packetizer.c`:

```c
#include "flx4_uac_packetizer.h"

void flx4_uac_packetizer_init(flx4_uac_packetizer_t *p,
                              uint32_t sample_rate,
                              uint8_t channels,
                              uint8_t bytes_per_sample)
{
    if (!p) return;
    p->sample_rate = sample_rate;
    p->channels = channels;
    p->bytes_per_sample = bytes_per_sample;
    p->frame_accum = 0u;
}

uint16_t flx4_uac_packetizer_next_frames(flx4_uac_packetizer_t *p)
{
    if (!p || p->sample_rate == 0u) return 0u;
    p->frame_accum += p->sample_rate;
    uint16_t frames = (uint16_t)(p->frame_accum / 1000u);
    p->frame_accum -= (uint32_t)frames * 1000u;
    return frames;
}

size_t flx4_uac_packetizer_next_bytes(flx4_uac_packetizer_t *p)
{
    uint16_t frames = flx4_uac_packetizer_next_frames(p);
    if (!p) return 0u;
    return (size_t)frames * (size_t)p->channels * (size_t)p->bytes_per_sample;
}
```

- [x] **Step 4: Run packetizer tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cd D:\Documents\DDJ-FFL4
make -C tests/flx4_usb_audio clean all
```

Expected:

- `flx4_uac_descriptors: PASS`
- `flx4_uac_packetizer: PASS`

Commit:

```powershell
git add firmware/control-board-s3/components/flx4_usb_audio tests/flx4_usb_audio
git commit -m "test: add flx4 usb audio packet scheduler"
```

---

## Task 4: P4 monitor PCM publisher API

**Files:**

- Create: `firmware/main-deck-p4/components/monitor_pcm_link/include/monitor_pcm_link.h`
- Create: `firmware/main-deck-p4/components/monitor_pcm_link/monitor_pcm_link.c`
- Create: `firmware/main-deck-p4/components/monitor_pcm_link/CMakeLists.txt`
- Create: `firmware/main-deck-p4/components/monitor_pcm_link/Kconfig`
- Create: `tests/monitor_pcm_link/test_monitor_pcm_link.c`
- Create: `tests/monitor_pcm_link/Makefile`
- Modify: `tests/run_p4_host_tests.ps1`

- [x] **Step 1: Define a non-blocking monitor sink**

Create `monitor_pcm_link.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint32_t submitted_blocks;
    uint32_t dropped_blocks;
    uint32_t submitted_frames;
} monitor_pcm_link_stats_t;

esp_err_t monitor_pcm_link_init(void);
esp_err_t monitor_pcm_link_set_format(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);
bool monitor_pcm_link_write_nonblocking(const int16_t *interleaved_stereo, size_t frames);
void monitor_pcm_link_get_stats(monitor_pcm_link_stats_t *out);
```

- [x] **Step 2: Write host tests for non-blocking behavior**

Create `tests/monitor_pcm_link/test_monitor_pcm_link.c`:

```c
#include <stdio.h>
#include <string.h>
#include "monitor_pcm_link.h"

#define EXPECT_TRUE(c,m) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); return 1; } } while (0)
#define EXPECT_EQ(a,b,m) do { if ((a)!=(b)) { fprintf(stderr, "FAIL: %s got=%u expected=%u\n", m, (unsigned)(a), (unsigned)(b)); return 1; } } while (0)

static int test_disabled_link_drops_without_failure(void)
{
    int16_t block[256 * 2] = { 0 };
    EXPECT_TRUE(monitor_pcm_link_init() == 0, "init ok");
    EXPECT_TRUE(monitor_pcm_link_set_format(48000u, 2u, 16u) == 0, "format ok");
    EXPECT_TRUE(!monitor_pcm_link_write_nonblocking(block, 256u), "disabled link reports not submitted");
    monitor_pcm_link_stats_t stats = { 0 };
    monitor_pcm_link_get_stats(&stats);
    EXPECT_EQ(stats.dropped_blocks, 1u, "drop counted");
    EXPECT_EQ(stats.submitted_blocks, 0u, "no submitted blocks");
    return 0;
}

int main(void)
{
    if (test_disabled_link_drops_without_failure() != 0) return 1;
    puts("monitor_pcm_link: PASS");
    return 0;
}
```

- [x] **Step 3: Implement disabled-safe first slice**

Create `monitor_pcm_link.c`:

```c
#include "monitor_pcm_link.h"
#include <string.h>

static monitor_pcm_link_stats_t s_stats;
static bool s_enabled;

esp_err_t monitor_pcm_link_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_enabled = false;
    return ESP_OK;
}

esp_err_t monitor_pcm_link_set_format(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    if (sample_rate == 0u || channels == 0u || bits_per_sample == 0u) return ESP_ERR_INVALID_ARG;
    s_stats.sample_rate = sample_rate;
    s_stats.channels = channels;
    s_stats.bits_per_sample = bits_per_sample;
    return ESP_OK;
}

bool monitor_pcm_link_write_nonblocking(const int16_t *interleaved_stereo, size_t frames)
{
    if (!interleaved_stereo || frames == 0u || !s_enabled) {
        s_stats.dropped_blocks++;
        return false;
    }
    s_stats.submitted_blocks++;
    s_stats.submitted_frames += (uint32_t)frames;
    return true;
}

void monitor_pcm_link_get_stats(monitor_pcm_link_stats_t *out)
{
    if (out) *out = s_stats;
}
```

- [x] **Step 4: Run P4 host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cd D:\Documents\DDJ-FFL4
.\tests\run_p4_host_tests.ps1
```

Expected:

- `monitor_pcm_link: PASS`
- Existing P4 host tests still pass.

Commit:

```powershell
git add firmware/main-deck-p4/components/monitor_pcm_link tests/monitor_pcm_link tests/run_p4_host_tests.ps1
git commit -m "test: add nonblocking monitor pcm link facade"
```

---

## Task 5: P4 audio engine publishes `hp_out` without blocking

**Files:**

- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/CMakeLists.txt`
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
- Modify: `firmware/main-deck-p4/components/web_server/web_server.c`
- Modify: `tests/audio_engine/test_audio_engine.c`

- [x] **Step 1: Add monitor link dependency**

In `audio_engine/CMakeLists.txt`, add `monitor_pcm_link` to `REQUIRES`:

```cmake
REQUIRES log bsp_jc4880 esp_codec_dev esp_timer media_io_gate monitor_pcm_link
```

- [x] **Step 2: Initialize monitor link in `audio_engine_init()`**

Add include:

```c
#include "monitor_pcm_link.h"
```

Inside `audio_engine_init()`, after `s_output_sample_rate = 0;` and before logging success:

```c
ESP_RETURN_ON_ERROR(monitor_pcm_link_init(), TAG, "monitor_pcm_link_init failed");
```

- [x] **Step 3: Set link format when output codec opens**

In `audio_output_service_open_codec(uint32_t sample_rate)`, after `s_output_sample_rate = sample_rate;`:

```c
(void)monitor_pcm_link_set_format(sample_rate, 2u, 16u);
```

- [x] **Step 4: Publish `hp_out` after it is filled**

In `ae_output_task()`, after:

```c
hp_out[i * 2] = mix.headphone.left;
hp_out[i * 2 + 1] = mix.headphone.right;
```

and before ES8311 write:

```c
(void)monitor_pcm_link_write_nonblocking(hp_out, AE_OUT_FRAMES);
```

This call must stay before or after `esp_codec_dev_write()` only if it is non-blocking. If future transport implementation can block, it must move to a lower-priority publisher task fed by a lock-free/ring queue.

- [x] **Step 5: Expose diagnostics**

Extend `audio_engine_diagnostics_snapshot_t`:

```c
uint32_t usb_headphone_submitted_blocks;
uint32_t usb_headphone_dropped_blocks;
uint32_t usb_headphone_submitted_frames;
```

In `audio_engine_get_diagnostics_snapshot()`:

```c
monitor_pcm_link_stats_t monitor_stats = { 0 };
monitor_pcm_link_get_stats(&monitor_stats);
out_snapshot->usb_headphone_submitted_blocks = monitor_stats.submitted_blocks;
out_snapshot->usb_headphone_dropped_blocks = monitor_stats.dropped_blocks;
out_snapshot->usb_headphone_submitted_frames = monitor_stats.submitted_frames;
```

In `web_server.c`, expose under `diagnostics`:

```json
"usb_headphones":{"submitted_blocks":%u,"dropped_blocks":%u,"submitted_frames":%u}
```

- [x] **Step 6: Run verification**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cd D:\Documents\DDJ-FFL4
.\tests\run_p4_host_tests.ps1
```

Expected:

- Existing audio and diagnostics tests pass.
- No host test observes a changed mixer result.

Run P4 build:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected:

- Build exits `0`.

Commit:

```powershell
git add firmware/main-deck-p4/components/audio_engine firmware/main-deck-p4/components/web_server tests/audio_engine
git commit -m "feat: publish monitor pcm blocks from audio engine"
```

---

## Task 6: Decide and implement dedicated P4-to-S3 PCM transport

**Files:**

- Modify: `docs/HARDWARE_WIRING.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `firmware/main-deck-p4/components/monitor_pcm_link/monitor_pcm_link.c`
- Modify: `firmware/main-deck-p4/components/monitor_pcm_link/Kconfig`
- Modify: `firmware/control-board-s3/components/p4_audio_link/p4_audio_link.c`
- Modify: `firmware/control-board-s3/components/p4_audio_link/Kconfig`
- Create: `docs/validation/P4_S3_AUDIO_LINK_BENCH.md`

- [x] **Step 1: Select physical transport**

Use this decision table:

| Candidate | Accept if | Reject if |
| --- | --- | --- |
| Dedicated I2S P4 TX master -> S3 RX slave | P4 GPIO32/GPIO34/GPIO35 plus ground are wired to S3 GPIO15/GPIO16/GPIO17; optional GPIO49/GPIO18 is left disconnected unless flow/debug is enabled; `CONFIG_DDJ_FLX4_HOST_MODE` keeps legacy `panel_io` inactive; FLX4 MIDI stays responsive at 44.1/48 kHz stereo 16-bit | S3 I2S RX cannot operate reliably with USB host load, legacy panel mode is enabled, DMA instability, or sustained underruns |
| Dedicated SPI P4 master -> S3 slave | I2S is rejected, four safe signal pins plus ground are available, stable at 8 MHz or higher, and no conflict with display, SDMMC, PCM5102A, S3 USB, or existing UART | Pin conflict, unstable DMA, or wiring risk |
| Dedicated UART P4 -> S3 | One safe TX/RX pair plus ground is available; stable at 4 Mbaud or higher; first slice limited to stereo 16-bit | Shared with control link, unstable framing, or overhead causes underruns |
| Move FLX4 USB host to P4 | S3 transport is infeasible and P4 USB topology can host both storage and FLX4 through a validated hub | USB storage conflicts, hub instability, or P4 UI/audio timing regresses |

Record the decision in `docs/validation/P4_S3_AUDIO_LINK_BENCH.md`.

- [x] **Step 2: Define common PCM block header**

Use the same binary header on both P4 and S3:

```c
typedef struct {
    uint32_t magic;          /* 'P4HP' little endian: 0x50483450 */
    uint16_t header_bytes;   /* sizeof(p4_audio_link_block_header_t) */
    uint16_t frames;         /* stereo frames following header */
    uint32_t sample_rate;    /* 44100 or 48000 first slice */
    uint32_t sequence;
    uint32_t payload_crc32;  /* crc of PCM payload, optional for diagnostics build */
} p4_audio_link_block_header_t;
```

PCM payload:

```text
int16 little-endian interleaved stereo: L0 R0 L1 R1 ...
```

- [x] **Step 3: Implement S3 receiver ring**

`p4_audio_link.c` must expose:

```c
typedef struct {
    uint32_t received_blocks;
    uint32_t sequence_gaps;
    uint32_t crc_errors;
    uint32_t ring_frames;
    uint32_t ring_capacity_frames;
    uint32_t underruns;
    uint32_t overruns;
    uint32_t sample_rate;
} p4_audio_link_stats_t;

esp_err_t p4_audio_link_init(void);
size_t p4_audio_link_read_frames(int16_t *dst_interleaved_stereo, size_t frames);
void p4_audio_link_get_stats(p4_audio_link_stats_t *out);
```

Ring policy:

- Store at least `4096` stereo frames.
- If producer overruns consumer, drop oldest monitor frames, not newest.
- If consumer underruns, return silence and increment `underruns`.
- Keep stats update atomic enough for diagnostics; do not log per block.

- [x] **Step 4: Implement P4 sender**

`monitor_pcm_link_write_nonblocking()` must enqueue the latest `hp_out` block to the selected physical transport. Rules:

- If transport queue has no free slot, increment `dropped_blocks` and return `false`.
- If transport accepts the block, increment `submitted_blocks/submitted_frames` and return `true`.
- Do not call blocking SPI/UART APIs inside P4 audio output task.
- A separate P4 transport task may block on the hardware peripheral after taking blocks from the monitor PCM queue.

- [ ] **Step 5: Bench transport without USB Audio**

P4 test mode:

- Generate a deterministic 1 kHz stereo sine or counter ramp through `monitor_pcm_link`.
- S3 receives it into `p4_audio_link` ring.
- S3 logs aggregate stats every 1 second:

```text
P4_AUDIO_LINK rx blocks=... gaps=0 crc=0 ring=... underruns=0 overruns=0
```

Expected for 60 seconds:

- `sequence_gaps=0`
- `crc_errors=0`
- `underruns=0` after initial startup
- `overruns=0` at steady state
- S3 MIDI input still responsive while link runs

Commit:

```powershell
git add firmware/main-deck-p4/components/monitor_pcm_link firmware/control-board-s3/components/p4_audio_link docs/HARDWARE_WIRING.md docs/ARCHITECTURE.md docs/validation/P4_S3_AUDIO_LINK_BENCH.md
git commit -m "feat: add dedicated p4 to s3 monitor pcm link"
```

---

## Task 7: S3 USB Audio Class streamer with synthetic tone

**Files:**

- Modify: `firmware/control-board-s3/components/flx4_usb_audio/include/flx4_usb_audio.h`
- Modify: `firmware/control-board-s3/components/flx4_usb_audio/flx4_usb_audio.c`
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c`
- Modify: `firmware/control-board-s3/main/app_main.c`
- Create: `docs/validation/FLX4_USB_AUDIO_TONE_SMOKE.md`

- [ ] **Step 1: Add USB Audio runtime API**

`flx4_usb_audio.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "usb/usb_host.h"
#include "flx4_uac_descriptors.h"

typedef struct {
    bool configured;
    flx4_uac_playback_format_t format;
    uint32_t submitted_packets;
    uint32_t completed_packets;
    uint32_t skipped_packets;
    uint32_t underrun_packets;
    uint32_t actual_bytes;
} flx4_usb_audio_stats_t;

esp_err_t flx4_usb_audio_configure(usb_host_client_handle_t client,
                                   usb_device_handle_t device,
                                   const uint8_t *config_desc,
                                   size_t config_len);
esp_err_t flx4_usb_audio_start_tone(uint16_t hz);
esp_err_t flx4_usb_audio_start_ring(void);
void flx4_usb_audio_stop(void);
void flx4_usb_audio_get_stats(flx4_usb_audio_stats_t *out);
```

- [ ] **Step 2: Configure audio interface**

In `flx4_usb_audio_configure()`:

1. Parse active descriptor with `flx4_uac_parse_playback_formats()`.
2. Select preferred format with `flx4_uac_select_preferred_format()`.
3. Claim selected interface and alternate setting:

```c
usb_host_interface_claim(client, device, selected.interface_num, selected.alternate_setting);
```

4. Allocate at least four isochronous transfers:

```c
usb_host_transfer_alloc(max_payload_per_urb, packets_per_urb, &transfer);
```

5. Do not start transfers until `flx4_usb_audio_start_tone()` or `flx4_usb_audio_start_ring()` is called.

- [ ] **Step 3: Implement tone mode before P4 link integration**

Tone mode proves FLX4 UAC streaming independently from P4 transport:

- 1 kHz sine, -18 dBFS, stereo.
- If selected format is 4-channel, send:
  - ch1/ch2: silence for first smoke pass;
  - ch3/ch4: tone for headphones candidate.
- Add a compile-time switch to invert that mapping for RCA/headphones identification:

```kconfig
config DDJ_FLX4_USB_AUDIO_TONE_ON_CHANNELS_1_2
    bool "Route FLX4 USB audio tone to channels 1/2 instead of headphone candidate channels"
    default n
```

- [ ] **Step 4: Hardware smoke tone mode**

Flash S3 and connect headphones to the DDJ-FLX4.

Expected:

- MIDI controls remain responsive.
- Tone is audible on FLX4 headphones for the accepted channel mapping.
- If 4-channel format is used, channel mapping is documented:
  - channels 1/2 drive FLX4 RCA or are silent;
  - channels 3/4 drive FLX4 headphones, or the reverse if measured.
- No S3 reboot.
- No continuous USB transfer errors.

Record:

```markdown
# FLX4 USB Audio Tone Smoke

Date:
S3 commit:
Descriptor accepted format:
Tone sample rate:
Channels:
Bits:
Audible on headphones:
Audible on FLX4 RCA:
MIDI responsive while streaming:
USB skipped packets:
USB underrun packets:
Decision:
```

Commit:

```powershell
git add firmware/control-board-s3/components/flx4_usb_audio firmware/control-board-s3/components/flx4_midi_host firmware/control-board-s3/main docs/validation/FLX4_USB_AUDIO_TONE_SMOKE.md
git commit -m "feat: stream usb audio tone to flx4"
```

---

## Task 8: End-to-end P4 headphone PCM to FLX4 USB Audio

**Files:**

- Modify: `firmware/control-board-s3/components/flx4_usb_audio/flx4_usb_audio.c`
- Modify: `firmware/control-board-s3/components/p4_audio_link/p4_audio_link.c`
- Modify: `firmware/main-deck-p4/components/monitor_pcm_link/monitor_pcm_link.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `firmware/main-deck-p4/components/web_server/web_server.c`

- [ ] **Step 1: Feed UAC packets from S3 P4 audio ring**

In the USB Audio transfer fill routine:

```c
size_t frames_needed = flx4_uac_packetizer_next_frames(&packetizer);
int16_t stereo[64 * 2];
size_t got = p4_audio_link_read_frames(stereo, frames_needed);
if (got < frames_needed) {
    memset(&stereo[got * 2], 0, (frames_needed - got) * 2u * sizeof(int16_t));
    stats.underrun_packets++;
}
```

Channel mapping:

- If selected format is stereo, copy P4 `hp_out` L/R to channels 1/2.
- If selected format is 4-channel and smoke proved headphones are channels 3/4:
  - channels 1/2: silence in first product slice;
  - channels 3/4: P4 `hp_out`.
- If selected format is 4-channel and smoke proved headphones are channels 1/2:
  - channels 1/2: P4 `hp_out`;
  - channels 3/4: silence.

- [ ] **Step 2: Add startup sequencing**

Startup order:

1. S3 starts FLX4 USB host and MIDI path.
2. S3 configures FLX4 USB Audio interface but does not stream non-silence until ring has data.
3. P4 boots and starts audio engine.
4. P4 monitor PCM link publishes blocks when output service is open.
5. S3 starts USB Audio ring mode after `p4_audio_link` reports valid sample rate and at least 20 ms of buffered frames.

- [ ] **Step 3: Handle sample-rate changes**

When S3 observes a P4 audio link block with a sample rate different from the current USB Audio streaming rate:

1. Stop FLX4 USB Audio transfers.
2. Release audio streaming interface.
3. Select matching FLX4 format for the new sample rate.
4. Claim interface/alternate setting.
5. Clear the S3 PCM ring.
6. Restart streaming with silence until at least 20 ms of frames are buffered.

If FLX4 does not expose the requested sample rate:

- keep USB Audio at 48 kHz;
- create a separate P4 or S3 sample-rate conversion plan before accepting that scenario for product use.

- [ ] **Step 4: Hardware smoke end-to-end**

Test matrix:

| Scenario | Expected |
| --- | --- |
| Deck 1 plays, PFL D1 on, Headphones Mix minimum | Deck 1 cue audible in FLX4 headphones |
| Deck 2 plays, PFL D2 on, Headphones Mix minimum | Deck 2 cue audible in FLX4 headphones |
| Both PFL off, Headphones Mix minimum | silence or near-silence in FLX4 headphones |
| Headphones Mix maximum | master mix audible in FLX4 headphones |
| Cue mode Split Mono | master one side, cue one side, matching current P4 behavior |
| FLX4 MIDI Play/Pause while USB Audio streams | controller remains responsive |
| Both decks play for 5 minutes | no P4 reboot, no S3 reboot, no USB stream collapse |

Required logs:

- S3: `flx4_usb_audio` packet stats every 5 seconds.
- S3: `p4_audio_link` ring stats every 5 seconds.
- P4: `audio_engine` diagnostics snapshot includes monitor PCM submitted/dropped counts.

Commit:

```powershell
git add firmware/control-board-s3/components/flx4_usb_audio firmware/control-board-s3/components/p4_audio_link firmware/main-deck-p4/components/monitor_pcm_link firmware/main-deck-p4/components/audio_engine firmware/main-deck-p4/components/web_server
git commit -m "feat: route p4 monitor pcm to flx4 headphones"
```

---

## Task 9: Diagnostics, status, and operator controls

**Files:**

- Modify: `firmware/main-deck-p4/components/web_server/web_server.c`
- Modify: `firmware/main-deck-p4/components/ui/ui_status.c`
- Modify: `firmware/main-deck-p4/components/ui/ui_settings.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
- Modify: `docs/STARTUP_CHECKLIST.md`

- [ ] **Step 1: Add diagnostics fields**

Expose:

```json
"usb_headphones":{
  "enabled":true,
  "sample_rate":48000,
  "channels":4,
  "bits":16,
  "ring_frames":1234,
  "ring_capacity_frames":4096,
  "submitted_packets":123456,
  "skipped_packets":0,
  "underrun_packets":0,
  "p4_submitted_blocks":12345,
  "p4_dropped_blocks":0
}
```

- [ ] **Step 2: Add P4 UI status indicator**

Add a compact status string in the existing P4 status area:

```text
USB HP OK
USB HP UNDERRUN
USB HP OFF
```

Rules:

- `OK`: streaming enabled and no underrun increase in last status window.
- `UNDERRUN`: underrun counter increased.
- `OFF`: FLX4 USB Audio endpoint not configured or feature disabled.

- [ ] **Step 3: Add Settings toggle**

Add a Settings control:

```text
FLX4 HP: OFF / USB
```

Default:

- OFF until the first hardware smoke is accepted.
- After acceptance, decide whether default should become USB when FLX4 is connected.

Commit:

```powershell
git add firmware/main-deck-p4/components/web_server firmware/main-deck-p4/components/ui firmware/main-deck-p4/components/audio_engine docs/STARTUP_CHECKLIST.md
git commit -m "feat: expose flx4 usb headphone diagnostics"
```

---

## Task 10: Documentation update

**Files:**

- Modify: `README.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/HARDWARE_WIRING.md`
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`
- Modify: `docs/RISK_REGISTER.md`

- [ ] **Step 1: Update architecture**

Add:

- FLX4 headphones are driven by S3 USB Audio Class, fed by P4 monitor PCM.
- `0xA5` remains semantic-only.
- Dedicated P4->S3 PCM link owns monitor audio payload.
- P4 still owns Headphones Mix and PFL state.

- [ ] **Step 2: Update wiring**

Document chosen P4/S3 PCM transport pins by copying the validated rows from
`docs/validation/P4_S3_AUDIO_LINK_BENCH.md`. The committed
`docs/HARDWARE_WIRING.md` update must contain real GPIO names/numbers for every
signal. For the planned I2S-first bench, start from this fixed candidate
mapping:

```markdown
| Signal | P4 pin | S3 pin | Direction | Notes |
| --- | --- | --- | --- | --- |
| I2S BCLK | GPIO32 | GPIO15 | P4 -> S3 | monitor PCM bit clock |
| I2S WS/LRCK | GPIO34 | GPIO16 | P4 -> S3 | stereo frame sync |
| I2S DOUT | GPIO35 | GPIO17 | P4 -> S3 | monitor PCM payload from P4 `hp_out` |
| READY/FLOW/debug | GPIO49 | GPIO18 | direction selected by role | optional; do not wire unless the bench plan uses it |
| GND | GND | GND | shared | required |
```

Do not promote this candidate to final wiring until the S3 build and hardware
smoke prove `panel_io` is inactive in FLX4 host mode and GPIO15-GPIO18 are not
driven by legacy jog/browse code. If the bench selects SPI or UART instead of
I2S, document exactly the signals that were wired and validated.

- [ ] **Step 3: Update risk register**

Add risks:

- USB isochronous streaming can starve MIDI if transfer callbacks or logging are heavy.
- P4->S3 PCM transport can underrun if link speed or buffering is insufficient.
- 44.1 kHz USB packet scheduling can drift if fractional frame handling is wrong.
- FLX4 channel mapping may differ between firmware versions; descriptor capture and tone smoke are required before product enable.

- [ ] **Step 4: Run documentation verification**

Run:

```powershell
cd D:\Documents\DDJ-FFL4
git diff --check
git status --short
```

Expected:

- `git diff --check` exits `0`.
- Only intended documentation and source files are changed.

Commit:

```powershell
git add README.md docs/ARCHITECTURE.md docs/HARDWARE_WIRING.md docs/CONTROL_LINK_PROTOCOL.md docs/DEVELOPMENT_PLAN.md docs/STARTUP_CHECKLIST.md docs/RISK_REGISTER.md
git commit -m "docs: document flx4 usb headphone path"
```

---

## Task 11: Full verification before merge

**Files:**

- No source changes unless verification finds a defect.

- [ ] **Step 1: Run host tests**

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cd D:\Documents\DDJ-FFL4
.\tests\run_s3_host_tests.ps1
.\tests\run_p4_host_tests.ps1
```

Expected:

- All S3 host tests pass.
- All P4 host tests pass.

- [ ] **Step 2: Build both firmware targets**

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py build
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected:

- Both builds exit `0`.

- [ ] **Step 3: Hardware smoke**

Required devices:

- S3 on COM3.
- P4 on COM15.
- DDJ-FLX4 connected to S3 USB host.
- Headphones plugged into FLX4 3.5 mm headphones jack.
- RCA/PCM5102A main out connected to amplifier for regression check.

Smoke sequence:

1. Flash S3.
2. Flash P4.
3. Start S3 monitor.
4. Start P4 monitor.
5. Load Deck 1 and Deck 2.
6. Start Deck 1 only.
7. Turn on PFL Deck 1 and set Headphones Mix to cue.
8. Confirm Deck 1 cue in FLX4 headphones.
9. Start Deck 2.
10. Turn on PFL Deck 2 and confirm Deck 2 cue in FLX4 headphones.
11. Move Headphones Mix to master and confirm master blend in FLX4 headphones.
12. Confirm PCM5102A RCA main output still works.
13. Confirm MIDI Play/Pause, Browse, Beat Sync, Pad FX, and LEDs remain responsive.
14. Let both decks play for 5 minutes.

Acceptance:

- FLX4 headphones output follows P4 PFL and Headphones Mix.
- PCM5102A RCA main output remains normal.
- No P4 reboot.
- No S3 reboot.
- No audible sustained crackle.
- No sustained USB headphone underruns after startup.
- MIDI controls remain responsive.

- [ ] **Step 4: Final commit or fix**

If verification exposes defects, fix and repeat Steps 1-3.

If verification passes:

```powershell
git status --short
git log --oneline -5
```

Then merge or push according to the active branch policy for that session.

---

## Risks and mitigations

| Risk | Why it matters | Mitigation |
| --- | --- | --- |
| FLX4 audio endpoint/channel mapping is not what we expect | Audio may go to RCA instead of headphones or nowhere | Descriptor capture plus synthetic tone smoke before using P4 audio |
| S3 USB host cannot keep MIDI responsive while streaming isochronous audio | Controller becomes unusable | Keep MIDI callbacks lightweight, no per-packet logs, aggregate stats only, hardware smoke with aggressive Play/Pause/Browse while streaming |
| P4->S3 PCM link lacks bandwidth or stable timing | Headphone underruns or crackle | Use dedicated I2S first with P4 GPIO32/GPIO34/GPIO35, reserve GPIO49 for flow/debug only if needed, keep a ring buffer on S3, use a non-blocking P4 producer, and monitor underrun/overrun counters |
| 44.1 kHz fractional USB packet schedule drifts | Periodic click or long-term under/overrun | Host-tested packetizer must produce exactly 44100 frames per 1000 USB frames |
| USB Audio sample-rate change interrupts playback | Headphones drop during track changes | Stop/reclaim/restart UAC streaming on sample-rate changes, start with silence until ring is primed |
| New diagnostics/logs reintroduce waveform or audio stutter | Previous issues were log-sensitive | Aggregate logs at 1-5 second intervals only; no per-packet or per-audio-block INFO logs |
| Existing FLX4 VU/MIDI OUT queue gets starved | LED feedback and controller feel regress | Keep USB Audio transfers separate from MIDI OUT queue; VU remains low priority |

---

## Acceptance definition

This feature is complete only when all are true:

- FLX4 physical headphones jack plays the P4 monitor/cue mix.
- PFL Deck 1/Deck 2 and Headphones Mix behave the same as the current P4 ES8311 monitor path.
- PCM5102A RCA main output still works.
- Dual-deck playback remains normal.
- Waveforms remain usable.
- FLX4 MIDI controls remain responsive while USB Audio headphones stream.
- S3 and P4 builds pass.
- S3 and P4 host tests pass.
- Hardware smoke results are recorded in `docs/validation/`.

---

## Self-review

Spec coverage:

- Controller headphones path: covered by Tasks 1, 7, 8, and 11.
- Existing P4 monitor DSP reuse: covered by Tasks 4 and 5.
- S3 FLX4 USB ownership: covered by Tasks 2, 7, and 8.
- Control link protection: covered by architecture constraints and Task 6.
- Hardware verification: covered by Tasks 1, 6, 7, 8, and 11.
- Documentation: covered by Task 10.

Placeholder scan:

- No implementation step relies on undefined behavior outside the explicit
  descriptor capture, pin bench, and smoke-test gates.
- The only intentionally measured values are descriptor fields and physical pin assignments, and each has a concrete capture/bench document where the executor must record the measured result before continuing.

Type consistency:

- `monitor_pcm_link_*` APIs are defined before use in `audio_engine`.
- `flx4_uac_*` descriptor and packetizer APIs are defined before USB streamer integration.
- `p4_audio_link_*` receiver APIs are defined before S3 UAC ring mode consumes them.
