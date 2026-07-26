# Control Link Protocol

Status: current wire protocol, audited 2026-07-16. The fixed `0xA5` event layer
and variable-length `0xA6` bulk layer are both active in production firmware.

Pajoniiir keeps the inherited UART `control_link` as the internal protocol
between the ESP32-S3 and ESP32-P4.

## Current Frame

```text
[0] 0xA5        start byte
[1] type        event or command type
[2] id          control id
[3] val_lo      value LSB
[4] val_hi      value MSB
[5] seq         rolling sequence counter
[6] checksum    XOR of bytes [1]..[5]
```

Baud rate: `460800` (8N1). Both boards must match; see `UART_BAUD` in each side's `control_link_uart.c`.

## Existing Types

| Direction | Type | Meaning |
| --- | ---: | --- |
| S3 -> P4 | `0x01` | button |
| S3 -> P4 | `0x02` | encoder |
| S3 -> P4 | `0x03` | pitch |
| S3 -> P4 | `0x04` | heartbeat |
| S3 -> P4 | `0x82` | semantic state: FLX4 USB connection state, S3 Debug AP status |
| P4 -> S3 | `0x81` | LED |
| P4 -> S3 | `0x82` | semantic command: S3 Debug AP enable request |
| both | `0xA6` | variable-length bulk frame: controller/profile data + firmware status (see [0xA6 Bulk Frame Layer](#0xa6-bulk-frame-layer-controller-profiles-and-firmware-status)) |

The `0xA6` byte starts a separate variable-length frame that never collides
with the fixed 7-byte `0xA5` frame; a receiver routes on the start byte.

## Pajoniiir Semantic Namespace

The current frame has only one `id` byte and one 16-bit value. Pajoniiir keeps
that wire format for MVP and makes the `id` namespace deck-aware instead.

Implemented namespace layout:

```text
id range:       deck, mixer, browser, or system namespace
deck control:   id - deck namespace base (32 controls per deck)
value:          signed delta, 0/1 button state, or 0..16383 absolute value
```

Suggested namespaces:

| Namespace | ID range | Meaning |
| --- | ---: | --- |
| Deck 1 | `0x10`-`0x2F` | transport, jog, tempo, loop, pad mode/action |
| Deck 2 | `0x30`-`0x4F` | transport, jog, tempo, loop, pad mode/action |
| Mixer | `0x50`-`0x5F` | channel faders, crossfader, PFL, trim/EQ/filter monitor controls |
| Browser | `0x60`-`0x6F` | browse/load/navigation |
| System | `0x70`-`0x7F` | heartbeat, diagnostics, global system/audio controls |

The S3 and P4 headers carry matching constants for this layout. Host tests
verify that the shared MVP IDs, Smart control IDs, and FLX4 connection state
IDs stay aligned across both firmware targets.

This avoids changing the wire frame before the first MVP hardware integration
test.

## MVP Semantic IDs

| ID | Event | Value |
| ---: | --- | --- |
| `0x10` | Deck 1 Play | `0` release, `1` press |
| `0x11` | Deck 1 Cue | `0` release, `1` press |
| `0x12` | Deck 1 Jog scratch delta | signed delta |
| `0x13` | Deck 1 Jog bend delta | signed delta |
| `0x14` | Deck 1 Jog touch | `0` release, `1` touch |
| `0x15` | Deck 1 Tempo | `0..16383` |
| `0x16` | Deck 1 Shift | `0` release, `1` press |
| `0x17` | Deck 1 Cue+Shift track start | `0` release, `1` press |
| `0x18` | Deck 1 Beat Sync | `0` release, `1` press; toggles P4 sync-enabled state and applies one-shot BPM match; decks phase-align to a matching beat while preserving the reference deck's signed intra-beat offset when beatgrids are available |
| `0x19` | Deck 1 Tempo Range | `0` release, `1` press; cycles deck-local `±6%`, `±10%`, and `±16%` manual fader ranges |
| `0x1A`-`0x1E` | Deck 1 loop buttons | `0` release, `1` press; Loop In/Out, Reloop/Exit, halve, and double are implemented |
| `0x1F`-`0x20` | Deck 1 beat-jump buttons | `0` release, `1` press; beat-jump back/forward behavior is implemented |
| `0x21`-`0x24` | Deck 1 legacy pad mode select | `0` release, `1` press |
| `0x25` | Deck 1 pad action | packed pad mode/index/shift/press |
| `0x26`-`0x29` | Deck 1 extended pad mode select | `0` release, `1` press; unsupported Keyboard/Stems and Key Shift values are reserved/ignored |
| `0x2C` | Deck 1 extended action | packed `CTRL_DECK_EXT_ACTION_*` plus press bit; Censor, Sync Master, Reloop Stop, Loop Adjust In/Out, Quantize |
| `0x30` | Deck 2 Play | `0` release, `1` press |
| `0x31` | Deck 2 Cue | `0` release, `1` press |
| `0x32` | Deck 2 Jog scratch delta | signed delta |
| `0x33` | Deck 2 Jog bend delta | signed delta |
| `0x34` | Deck 2 Jog touch | `0` release, `1` touch |
| `0x35` | Deck 2 Tempo | `0..16383` |
| `0x36`-`0x49` | Deck 2 extension controls | same control order as Deck 1 |
| `0x4C` | Deck 2 extended action | same packed format as Deck 1 |
| `0x50` | Channel 1 volume | `0..16383` |
| `0x51` | Channel 2 volume | `0..16383` |
| `0x52` | Crossfader | `0..16383` |
| `0x53` | Deck 1 PFL | `0` release, `1` press/toggle |
| `0x54` | Deck 2 PFL | `0` release, `1` press/toggle |
| `0x55`-`0x5F` | Trim, EQ, Filter, Headphone Mix | `0..16383`; P4 applies trim/pregain, three-band EQ, Smart CFX filter use, and headphone mix DSP |
| `0x60` | Browse delta | signed delta |
| `0x61` | Load Deck 1 | `0` release, `1` press |
| `0x62` | Load Deck 2 | `0` release, `1` press |
| `0x63` | Browse press | `0` release, `1` press; toggles Library/Overview |
| `0x64` | Browse+Shift delta | signed delta; accelerated Library navigation or Overview zoom |
| `0x65` | Browse+Shift press | `0` release, `1` press; press forces Library view |
| `0x66` | Shift+Load Deck 1 | `0` release, `1` press; currently uses same P4 selected-browser-track load path as normal Load Deck 1 |
| `0x67` | Shift+Load Deck 2 | `0` release, `1` press; currently uses same P4 selected-browser-track load path as normal Load Deck 2 |
| `0x70` | FLX4 connection state | `0` disconnected, `1` connected; sent with `CTRL_TYPE_STATE` |
| `0x71` | Smart CFX | `0` release, `1` press; press toggles P4 Smart CFX state |
| `0x72` | Smart Fader | `0` release, `1` press; press toggles P4 Smart Fader state |
| `0x73` | Beat FX select next | `0` release, `1` press; P4 cycle is `FILTER → ECHO → FLANGER → DELAY → FILTER` (`NONE` excluded) |
| `0x74` | Beat FX select previous | `0` release, `1` press; P4 traverses the same cycle in exact reverse (`NONE` excluded) |
| `0x75` | Beat FX beat decrement | `0` release, `1` press; press moves P4 Beat FX beat size down |
| `0x76` | Beat FX beat increment | `0` release, `1` press; press moves P4 Beat FX beat size up |
| `0x77` | Beat FX target | `0` CH1, `1` CH2, `2` both; S3 derives `2` when both FLX4 target selector signals are active |
| `0x78` | Beat FX depth | `0..127`; sent with `CTRL_TYPE_PITCH` from FLX4 `0xB4/0x02` |
| `0x79` | Beat FX on/off | `0` release, `1` press; press toggles P4 Beat FX enabled state |
| `0x7A` | Beat FX clear | `0` release, `1` press; press resets P4 Beat FX state to defaults |
| `0x7B` | Master volume | `0..16383`; sent with `CTRL_TYPE_PITCH` from FLX4 Master Level `0xB6/0x08+0x28`; P4 applies it as runtime non-boosting master output volume |
| `0x7D` | Headphone Level | `0..16383`; sent with `CTRL_TYPE_PITCH` from FLX4 `0xB6/0x0D+0x2D`; P4 scales only headphone/monitor output |
| `0x7E` | Shift+Smart CFX | `0` release, `1` press; P4 consumes as no-op placeholder until standalone behavior is defined |
| `0x7F` | Shift+Smart Fader | `0` release, `1` press; P4 consumes as no-op placeholder until standalone behavior is defined |
| `0x83` | Shift+Beat FX beat decrement | `0` release, `1` press; press moves P4 Beat FX beat size down by two enum positions with min saturation |
| `0x84` | Shift+Beat FX beat increment | `0` release, `1` press; press moves P4 Beat FX beat size up by two enum positions with max saturation |
| `0x85` | S3 Debug AP | bidirectional on `CTRL_TYPE_STATE`; P4->S3 request `0` OFF / `1` ON; S3->P4 status `0` OFF / `1` STARTING / `2` ON / `3` ERROR (see below) |

In S3 translator mode, `flx4_map` converts the DDJ-FLX4 MIDI controls from
`docs/DDJ_FLX4_MIDI_MAP.md` into these semantic IDs. High-rate jog, tempo,
channel fader, and crossfader events are locally coalesced before UART send;
button edges and load/PFL events remain FIFO.

Jog-touch is the exception under saturation because it is a safety-critical
level, not an ordinary button edge. At both the S3 translator queue and P4
control-event queue, the newest touch level supersedes older queued levels for
the same platter and is inserted at the front. High-rate motion is the preferred
eviction victim; a button-only full queue sacrifices one oldest event rather
than dropping the latest platter release.

`CTRL_ID_HEADPHONE_LEVEL`, `CTRL_ID_SMART_CFX_SHIFT`,
`CTRL_ID_SMART_FADER_SHIFT`, `CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT`, and
`CTRL_ID_BEAT_FX_BEAT_INC_SHIFT` are literal `0x7D`, `0x7E`, `0x7F`, `0x83`,
and `0x84` values in both firmware targets because the current namespace
encoding aliases `CTRL_NS_SYSTEM | offset` values above `0x0F`.

Pad action values are packed into the signed 16-bit `value` field:

- bits `0..2`: pad index `0..7`;
- bits `3..5`: pad mode `0..7` (`HOT_CUE`, `BEAT_LOOP`, `BEAT_JUMP`,
  retained compatibility values for out-of-scope `KEY_SHIFT` and `KEYBOARD`,
  `PAD_FX1`, `PAD_FX2`, and retained compatibility value for out-of-scope
  `SAMPLER`);
- bit `6`: shifted pad action;
- bit `7`: pressed state.

Deck extended action values are packed into the signed 16-bit `value` field:

- bits `0..6`: `CTRL_DECK_EXT_ACTION_*`;
- bit `7`: pressed state.

## LED Feedback

The P4 sends LED commands only from confirmed state. For example, Deck 1
Play LED turns on only when P4 `deck_core` confirms Deck 1 is playing.

The current MVP LED payload uses inherited `led_id_t` values with the deck
packed into the high byte of the 16-bit value by `control_link_send_led_deck()`:

| LED ID | Meaning |
| ---: | --- |
| `0x00` | Cue |
| `0x01` | Play |
| `0x04` | PFL |
| `0x05` | VU meter level |
| `0x06` | Pad mode: Hot Cue |
| `0x07` | Pad mode: Keyboard/Stems compatibility ID; kept OFF/ignored |
| `0x08` | Pad mode: Pad FX1 |
| `0x09` | Pad mode: Pad FX2 |
| `0x0A` | Pad mode: Beat Jump |
| `0x0B` | Pad mode: Beat Loop |
| `0x0C` | Pad mode: Sampler compatibility ID; kept OFF/ignored |
| `0x0D` | Pad mode: Key Shift compatibility ID; kept OFF/ignored |
| `0x0E` | Beat Sync enabled |
| `0x0F` | Loop In indicator |
| `0x10` | Loop Out indicator |
| `0x34` | Master Cue monitor indicator |
| `0x35` | Censor active indicator |
| `0x36` | Cue+Shift / track-start output candidate |
| `0x37` | Loop Adjust In output candidate |
| `0x38` | Loop Adjust Out output candidate |
| `0x39` | Track Load Deck 1 illumination output candidate |
| `0x3A` | Track Load Deck 2 illumination output candidate |

`LED_REMOTE_COUNT` is `59`. Existing values through `LED_MASTER_CUE` remain
stable; the new FLX4 official/PDF output IDs are appended after
`LED_MASTER_CUE`.

```text
value low byte:  LED state, 0 off / 1 on / 2 blink
value high byte: deck, 0 Deck 1 / 1 Deck 2
```

States remain inherited:

- `0`: off
- `1`: on
- `2`: blink

`LED_VU_METER` uses the same deck-aware LED frame but interprets the low byte
as a FLX4 VU level `0..127`. P4 samples per-deck audio peaks from the output
path, resets the peak when read, and publishes levels at a controlled 30 ms
period. S3 forwards this as USB MIDI CC `0x02` on `0xB0` for Deck/Channel 1 and
`0xB1` for Deck/Channel 2.

P4 also owns reconnect recovery. When S3 reports
`CTRL_TYPE_STATE / CTRL_ID_FLX4_CONNECTION / CTRL_FLX4_CONNECTED`, P4 forces a
P4-owned LED snapshot for Deck 1/2 Cue, Play, PFL, selected supported pad mode,
Beat Sync enabled state, active Loop In/Out state, Beat Loop pad state,
momentary Pad FX pad state, Censor active state, Smart CFX/Fader state, the
global Beat FX ON/OFF enabled state, Track Load Deck 1/2 illumination, and the
global Master Cue monitor state.
Host tests verify normal diff
suppression, failed-send retry, and forced reconnect publication. Transport
reconnect smoke has passed for Play/Cue/PFL, extended pad-mode/sync/loop
reconnect, USB replug, S3 reset, and P4 heartbeat connected-state refresh where
recorded in the startup checklist.

The new Censor LED is snapshot-driven from P4 `deck_state_t.censor_active`.
Cue+Shift / track-start, Loop Adjust In, and Loop Adjust Out are P4-owned
momentary LED flashes. Track Load Deck 1/2 illumination follows P4 audio-engine
loaded state and is included in reconnect refresh. Beat Jump normal pad LEDs and
shifted helper LEDs 7/8 are implemented from P4 loaded-track/mode/shift state.
Sampler, Keyboard/Stems, and Key Shift mode/pad behavior is out of product scope
as of 2026-07-07; their numeric IDs remain reserved for compatibility and
reconnect OFF output only.

In DDJ-FLX4 translator mode, S3 also refreshes the already-connected FLX4 state
after each heartbeat while the USB MIDI device remains open. This is
level-triggered recovery in addition to edge-triggered USB connect/disconnect:
if P4 reboots while S3 and the FLX4 stay powered, the next heartbeat refresh
lets the freshly booted P4 force its LED snapshot without requiring a controller
replug.

After a successful heartbeat-driven FLX4 connection refresh, S3 also replays
the last known FLX4 absolute input snapshot to P4. The replay covers only
controls whose current value S3 has actually observed as a complete value:
channel faders, crossfader, trim/pregain, EQ high/mid/low, channel filter,
Master Level, Headphones Mix, Headphone Level, and Beat FX Level/Depth. S3 does
not fabricate defaults for unknown physical positions, and the USB MIDI class
does not expose a generic "read all knobs now" request. Tempo faders, buttons,
pad actions, PFL, browse, and jog inputs are deliberately excluded from this
snapshot phase because replaying them could create false user actions or disable
Beat Sync.

The current P4 Beat FX snapshot is exposed through `/api/status` under
`beat_fx` for low-rate hardware smoke verification without raw MIDI logging.
The object contains `effect`, its additive human-readable `effect_name`,
`beat`, `target`, `depth`, and `enabled`; numeric effect values are `0=NONE`,
`1=FILTER`, `2=ECHO`, `3=FLANGER`, and `4=DELAY`. The new
DELAY value does not change the wire protocol: S3 still sends only the semantic
Next/Previous events above, and P4 owns selection/state. DELAY is a full-band
one-shot repeat with Level/Depth as wet gain, while ECHO remains a damped
multi-repeat feedback effect. Time is derived from effective BPM when a Beat FX
state-changing event applies the audio state; it is not automatically retimed
by a later tempo, Beat Sync or track-load change. The valid BPM range is
40–300, with a 120 BPM fallback. Both modes cap delay time at 1000 ms; target
BOTH currently derives one shared time from Deck 1 BPM. They share the existing
per-deck stereo delay line without an additional PSRAM allocation.
`NONE` is a compatibility sentinel only; CLEAR restores disabled FILTER,
beat `1`, target BOTH and depth `64`. Diagnostics retain the `beat_fx_echo`
compatibility object with per-deck fields `allocated1/2`, `enabled1/2`,
`mode1/2` and `delay_ms1/2`. `mode1/2` is the last commanded/retained shared
lane mode, and `delay_ms1/2` becomes zero while that lane is disabled.
`enabled1/2` means the DSP lane is actually active: top-level Beat FX is ON,
depth is nonzero, that deck is targeted, and its buffer is allocated. It can
therefore be false while top-level `beat_fx.enabled` and the controller LED are
true. Tail-ringing state is not exposed. Use top-level
`beat_fx.effect_name` as the selected-effect authority. Flanger and DELAY
hardware smoke remains pending.

The audio engine, not `deck_core`, maps raw depth through a square-root wet
taper up to 0.70. Echo feedback spans 0.20–0.68; Delay forces feedback to zero.
Wet/feedback gains ramp, but a live beat-size change moves the delay read head
immediately without time interpolation. On switch-off or CLEAR, Echo can ring
for about 2 s and Delay keeps exactly its previous delay period so the pending
tap can leave the line. A live Echo↔Delay mode change resets the shared line;
tail state is intentionally absent from the diagnostics object.
The top-level Beat FX `enabled` state also drives the physical FLX4 Beat FX
ON/OFF LED via `LED_BEAT_FX_ON`. The production P4 snapshot emits the
global/deck-1 LED lane,
which S3 maps to note `0x47` on `0x94`; the generic S3 encoder also supports the
deck-2 `0x95` form for profile/compatibility use.

The physical FLX4 MASTER CUE button is mapped from the official MIDI list to
`CTRL_ID_MASTER_CUE` (`0x96/0x63`, shifted `0x96/0x78`). P4 owns the monitor
state: a button press toggles whether the master side contributes to the
headphone/monitor mix, while the main/RCA master output remains unchanged.
The P4 LED snapshot includes `LED_MASTER_CUE`; S3 maps it to output note
`0x63` on `0x96`.

Pad mode LEDs are also P4-owned. `deck_core` stores controller `pad_mode`
separately from the legacy `perf_mode`, so deferred modes such as `PAD_FX1`,
`PAD_FX2`, `KEYBOARD`, and `SAMPLER` can still drive LED state without implying
that their pad behavior is implemented. The S3 maps these LED IDs to the
XML-derived FLX4 MIDI output notes and sends exactly one active pad-mode LED per
deck in the P4 snapshot.

Pad FX pad LED feedback is also P4-owned. `deck_core` stores the current
momentary Pad FX pad press per deck and publishes normal Pad FX1/Pad FX2 pad
LEDs through the same snapshot path. The LEDs follow the physical press/release
state only; Pad FX Echo audio tails intentionally continue after release without
holding the pad LED on.

Beat Sync LED feedback is P4-owned from `deck_core.sync_enabled`. Pressing Beat
Sync toggles the target deck's sync state, applies the current one-shot BPM
match and signed intra-beat phase-align behavior, republishes the LED snapshot,
and S3 maps `LED_SYNC` to USB MIDI note `0x58` on `0x90`/`0x91`.

Loop In and Loop Out LED feedback is also P4-owned. P4 combines the deck-core
pending loop-in marker with the authoritative per-deck audio loop state from
`audio_engine_deck_get_loop_state()`. `LED_LOOP_IN` turns on as soon as Loop In
sets a pending marker and remains on while an active loop exists. `LED_LOOP_OUT`
turns on only after Loop Out closes an active loop. S3 maps those LED IDs to USB
MIDI notes `0x10` and `0x11` on `0x90`/`0x91`.

## S3 Debug AP Control

`CTRL_ID_S3_DEBUG_AP` (`0x85`) drives the runtime, bench-only S3 Wi-Fi debug
access point and live log viewer (see `docs/S3_WIFI_DEBUG_LOG.md`). It is a
diagnostic control, not an audio/deck control, and rides on the existing 7-byte
frame with `CTRL_TYPE_STATE` (`0x82`) in both directions.

Request (P4 -> S3):

| Field | Value |
| --- | --- |
| type | `CTRL_TYPE_STATE` (`0x82`) |
| id | `CTRL_ID_S3_DEBUG_AP` (`0x85`) |
| value | `0` request OFF, `1` request ON |

Status feedback (S3 -> P4), `ctrl_s3_debug_ap_status_t`:

| Value | Status | Meaning |
| ---: | --- | --- |
| `0` | `OFF` | AP and HTTP server stopped |
| `1` | `STARTING` | S3 is bringing up SoftAP/HTTP |
| `2` | `ON` | WPA2 AP live at `Pajoniiir-S3-DEBUG` / `http://192.168.4.1` |
| `3` | `ERROR` | start failed; S3 tore down partial state and stays OFF |

Handshake and ownership:

- The switch lives only in the **P4 Settings UI**. It is **not** persisted in P4
  NVS and always initializes OFF after a P4 boot.
- On P4 boot (and after S3 reconnect recovery) P4 sends `0x85 = 0` as a safe
  reset so the debug AP never lingers active across a reboot.
- The P4 UI toggle calls `control_link_send_state(CTRL_ID_S3_DEBUG_AP, 0/1)`.
- S3 dispatches the request to `s3_debug_ap_request()` and reports every state
  transition back through its status callback, which P4 `deck_core` forwards to
  the Settings label (`OFF` / `STARTING` / `ON` / `ERROR`).
- FLX4 MIDI, control-link UART, and P4-to-S3 headphone audio must keep running
  regardless of debug AP state; a start failure only yields `ERROR`.

The `control_link_protocol` host test asserts `CTRL_ID_S3_DEBUG_AP` and the
`CTRL_S3_DEBUG_AP_*` status enum stay byte-for-byte aligned across both targets.

## 0xA6 Bulk Frame Layer (controller profiles and firmware status)

The 7-byte `0xA5` frame carries small semantic events. Payloads that do not fit
it — the connected-controller descriptor, compiled controller profiles, and S3
firmware report — use a second, variable-length frame type on the **same UART**,
distinguished by a different start byte (`0xA6`). This is the transport for the
data-driven multi-controller platform (see `docs/CONTROLLER_PROFILE_SCHEMA.md`)
and low-rate system metadata.

Frame layout:

```text
[0] 0xA6        start byte
[1] type        CTRL_BULK_TYPE_* below
[2] seq         rolling sequence counter
[3] len         payload length, 0..128
[4..4+len)      payload
[4+len]         crc16_lo
[5+len]         crc16_hi
```

`crc16` is CRC16-CCITT (poly `0x1021`, init `0xFFFF`, no reflection) over bytes
`[1 .. 4+len)`. The frame codec (`ctrl_bulk.c`) and the profile-transfer
receiver (`cp_xfer.c`) are kept **byte-for-byte identical** on the S3 and P4
sides; the S3 host runner asserts the two file copies match, and
`control_link_protocol` asserts the shared constants agree.

### P4 receive-health telemetry

The P4 counts every valid S3-to-P4 `0xA5` and `0xA6` frame against the S3's
shared rolling sequence. The first valid frame establishes the baseline;
subsequent non-contiguous values increment one `sequence_gaps` event, including
across the `255 -> 0` wrap. Invalid `0xA5` checksums and invalid `0xA6`
CRC/format frames increment separate counters and are not accepted as sequence
baselines.

`GET /api/status` exposes the snapshot under `control_link`:
`connected`, `heartbeat_age_ms`, `rx_frames`, `sequence_gaps`, combined
`crc_errors`, `event_checksum_errors`, `bulk_frames`, `bulk_crc_errors`,
`last_sequence`, and `sequence_valid`. The periodic P4 health monitor also
emits `CONTROL_LINK_CRC_ERROR` and `CONTROL_LINK_GAP` service-journal records
when their totals change. These are low-rate summaries, not a raw UART trace.

The same status response exposes service-journal health under `service_log`:
`available`, `queue_depth`, `queue_capacity`, `dropped`, `written`,
`current_bytes`, and `last_error`.

| Type | Dir | Meaning |
| ---: | --- | --- |
| `0x01` CONTROLLER_DESCRIPTOR | S3 -> P4 | connected controller VID/PID + capability bits + product string |
| `0x02` PROFILE_BEGIN | P4 -> S3 | total_size + transfer crc32 + vid/pid |
| `0x03` PROFILE_CHUNK | P4 -> S3 | offset + profile bytes |
| `0x04` PROFILE_END | P4 -> S3 | end of stream (triggers crc32 verify) |
| `0x05` PROFILE_ACK | S3 -> P4 | acked frame type |
| `0x06` PROFILE_NACK | S3 -> P4 | nacked frame type + reason |
| `0x07` PROFILE_ACTIVATE | P4 -> S3 | activate the received profile |
| `0x08` PROFILE_STATUS | S3 -> P4 | transfer state + vid/pid |
| `0x09` PROFILE_CLEAR | P4 -> S3 | drop the active profile (fall back to built-in) |
| `0x0A` FIRMWARE_REPORT | S3 -> P4 | running slot + image state + 32-byte version |

### Controller descriptor report (S3 -> P4)

When the S3 opens a controller it sends `CONTROLLER_DESCRIPTOR` with VID, PID,
capability bits (`CTRL_DESC_CAP_MIDI_IN/MIDI_OUT/USB_AUDIO`), and a 32-byte
product string. It is re-sent with every connection-state publish and heartbeat
refresh, so a P4-only reboot re-learns the controller. The P4
`controller_profile_manager` matches the VID/PID against profiles on the SD/TF
card and selects the active profile.

The S3 also sends the semantic
`CTRL_ID_FLX4_CONNECTION=CTRL_FLX4_DISCONNECTED` state when the USB controller
closes. P4 treats that state as the authoritative removal edge, clears only the
live controller/profile selection (the scanned profile inventory remains
loaded), cancels activation of an in-flight stale transfer, and emits one
`CONTROLLER_DISCONNECTED` service-journal record. A later descriptor can match
and activate the controller again.

### Profile transfer (P4 -> S3)

On a VID/PID match the P4 streams the compiled `.s3bin` to the S3 off the RX
task: `BEGIN`, sequential in-order `CHUNK`s (each with a byte offset so a drop
is caught immediately), then `END`. The S3 reassembles into a buffer, verifies
the transfer `crc32`, and `ACK`s (or `NACK`s with `ctrl_profile_nack_t`:
`SIZE`/`STATE`/`OFFSET`/`CRC`/`PARSE`). The P4 then sends `ACTIVATE`; the S3
parses the stored blob and installs it as the active profile. The P4 sender
waits for each stage's ACK with retry + timeout; a `NACK` restarts the transfer.
Because the S3CP file carries its own internal crc32 in addition to the
transfer crc32, a profile is double-checked before use.

### S3 firmware report (S3 -> P4)

S3 sends `FIRMWARE_REPORT` immediately with its first heartbeat and every five
seconds afterward. Periodic publication lets a P4-only reboot recover the peer
status without adding a request/response state machine.

Payload (`34` bytes):

```text
[0]      slot     0 unknown, 1 ota_0, 2 ota_1, 3 factory
[1]      state    0 unknown, 1 new, 2 pending_verify, 3 valid,
                  4 invalid, 5 aborted
[2..34)  version  32-byte NUL-padded ESP app version
```

P4 stores the latest report under a short critical section. Settings shows the
real P4 and S3 version/slot/state, and P4 `GET /api/firmware` includes the same
S3 data. The report carries metadata only; firmware binaries are never
forwarded over UART.

For OTA acceptance, the nested P4 response field `s3.state` is the S3
ESP-IDF image state from this report and can be used to confirm `valid`. The
top-level `state` returned by either target's own HTTP OTA service is instead
the transfer state (`idle`, `receiving`, `ready_to_reboot` or `failed`) and must
not be misread as partition validity.

### Dynamic mapping and fallback

Once a profile is active, the S3 maps controller MIDI IN through the profile's
input table (`controller_profile_runtime`) instead of the built-in `flx4_map`,
and maps P4 LED frames through the profile's output table instead of
`flx4_led_midi`. When no profile is active it falls back to the built-in FLX4
map on both paths. The FLX4 `profile.s3bin` is proven byte-equivalent to the
built-in map by a golden-parity host test, so the dynamic path reproduces the
built-in behaviour exactly.

## Future Protocol Versioning

After MVP, consider a versioned 8-byte or 9-byte frame with explicit deck and
control fields. Do not change frame length before the first S3/P4 integration
test unless the 7-byte frame blocks a real requirement.
