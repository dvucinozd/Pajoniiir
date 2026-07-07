# S3 Debug AP Log Viewer Design

## Goal

Add an on-demand S3-hosted Wi-Fi debug AP for bench diagnostics when the S3
USB-UART adapter is disconnected. The feature is disabled by default and can
only be enabled from the P4 Settings UI during the current runtime session.

## User-Facing Behavior

- P4 Settings gets a separate temporary switch named `S3 DEBUG AP`.
- The switch is always OFF after P4 boot and is not persisted in P4 NVS.
- Enabling the switch sends a command to the S3 over the existing `0xA5`
  `control_link`.
- S3 starts an open SoftAP:
  - SSID: `PajoNiiiR-S3-DEBUG`
  - IP: `192.168.4.1`
- A browser connected to the AP can open `http://192.168.4.1` and see a live
  S3 log viewer.
- The web page is read-only. It does not expose remote commands.
- Disabling the switch stops the S3 debug AP and web server.

## Architecture

The P4 remains responsible for the physical settings UI. The S3 remains
responsible for its own Wi-Fi radio, AP mode, HTTP server, and log capture.

The P4 sends a new P4-to-S3 control command:

```text
CTRL_TYPE_STATE / CTRL_ID_S3_DEBUG_AP / value
```

Values:

```text
0 = request OFF
1 = request ON
```

The S3 replies with a new S3-to-P4 status message using the same ID:

```text
CTRL_TYPE_STATE / CTRL_ID_S3_DEBUG_AP / status
```

Statuses:

```text
0 = OFF
1 = STARTING
2 = ON
3 = ERROR
```

The current 7-byte `0xA5` frame format is unchanged.

## P4 Settings UI

The existing `WI-FI REMOTE` switch remains separate. It controls the existing
P4 Wi-Fi remote feature.

The new `S3 DEBUG AP` switch:

- is not backed by `app_settings`;
- initializes OFF every time the settings screen is created;
- sends ON/OFF requests through the P4 control-link TX path;
- displays the latest S3-reported status: `OFF`, `STARTING`, `ON`, or `ERROR`;
- may optimistically show `STARTING` after an ON request until an S3 status
  frame arrives.

On P4 boot or S3 reconnect/heartbeat recovery, P4 should send OFF as a safe
reset so the debug AP does not accidentally remain active after reboot.

## S3 Runtime

The existing build-time UDP Wi-Fi debug log is replaced or bypassed for this
runtime-controlled AP mode. The default runtime state is OFF.

When ON is requested:

1. S3 reports `STARTING`.
2. S3 initializes Wi-Fi/netif/event-loop state if needed.
3. S3 starts SoftAP mode with the configured SSID and `192.168.4.1`.
4. S3 starts a small HTTP server.
5. S3 installs or activates a non-blocking ESP log hook.
6. S3 reports `ON`.

When OFF is requested:

1. S3 stops accepting web clients.
2. S3 stops the HTTP server.
3. S3 stops SoftAP mode if no other S3 feature uses Wi-Fi.
4. S3 deactivates the live debug stream.
5. S3 reports `OFF`.

If start fails, S3 logs the error, stops any partially-started resources, and
reports `ERROR`. FLX4 MIDI, control-link UART, and P4-to-S3 headphone audio
streaming must continue operating.

## Web Log Viewer

The page served at `/` is intentionally minimal:

- title: `S3 Debug Log`;
- current connection target text: `PajoNiiiR-S3-DEBUG / http://192.168.4.1`;
- live scrolling log region;
- automatic reconnect if the stream disconnects.

The live stream should use Server-Sent Events at `/events` unless ESP-IDF HTTP
server constraints make polling simpler. The browser must not be able to send
commands to the firmware.

## Log Capture Safety

The log hook must never block critical firmware paths.

Requirements:

- preserve normal UART logging;
- copy log output into a fixed-size ring buffer;
- truncate individual lines to a bounded maximum;
- drop or overwrite logs under pressure instead of blocking;
- perform HTTP/SSE writes from web-server context, not from the log hook;
- avoid per-MIDI-packet and per-audio-block INFO logging.

The feature is intended for aggregate diagnostics such as `P4_AUDIO_LINK` and
`FLX4_USB_AUDIO` health counters.

## Test Plan

Host tests should cover behavior that can be verified without hardware:

- S3/P4 protocol constants match for `CTRL_ID_S3_DEBUG_AP`.
- S3 debug status enum values match expected wire values.
- S3 log ring buffer appends, truncates, and handles overflow without blocking.
- S3 command handler is OFF by default, transitions through STARTING, and
  reports ERROR on injected start failure.
- P4 settings helper/callback path sends requests without saving the setting
  to NVS.

Firmware verification:

- `tests/run_s3_host_tests.ps1`
- `tests/run_p4_host_tests.ps1`
- `idf.py build` for `firmware/control-board-s3`
- `idf.py build` for `firmware/main-deck-p4`

Hardware smoke:

1. Boot with S3 debug AP OFF.
2. Confirm no `PajoNiiiR-S3-DEBUG` AP is visible.
3. Enable `S3 DEBUG AP` in P4 Settings.
4. Confirm P4 UI shows `STARTING` then `ON`.
5. Connect a phone/laptop to `PajoNiiiR-S3-DEBUG`.
6. Open `http://192.168.4.1`.
7. Confirm live S3 logs update while FLX4 MIDI/audio remains responsive.
8. Disable `S3 DEBUG AP`.
9. Confirm AP disappears and P4 UI shows `OFF`.

## Risks

- Wi-Fi/AP consumes RAM, CPU, and power when enabled.
- Excessive logging can still introduce jitter if high-rate logs are reenabled.
- Open AP has no authentication, so it must remain a bench/debug-only feature.
- P4 and S3 status can briefly disagree during reset or UART interruption; the
  ack/status path is used to make this visible instead of hidden.

## Out of Scope

- Remote command execution from the browser.
- Persistent S3 debug AP enablement in P4 NVS.
- Connecting the S3 to an existing Wi-Fi network.
- Replacing the P4 Wi-Fi remote UI.
- Changing the `0xA5` frame length or adding a new transport.
