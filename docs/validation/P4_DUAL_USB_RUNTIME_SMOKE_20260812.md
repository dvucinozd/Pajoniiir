# P4 dual-USB runtime smoke — 2026-08-12

## Scope and verdict

This record covers the unfinished `feat/p4-dual-usb-host` bench session. It is
positive evidence for direct P4 dual-root enumeration and for the upstream USB
recycle fix, but it is **not** merge or release acceptance.

Verdict: **blocked by 5 V/VBUS stability**. The most controlled dual-deck run
ended in a hardware-reported brownout even after the FLX4 was disconnected.

## Confirmed behavior

- USB0 mounted the Rekordbox drive and exposed a 191-track library.
- USB1 enumerated the DDJ-FLX4 directly as VID:PID `2B73:0045`; the local
  controller reached USB-MIDI ready and operator interaction was observed.
- Both P4 roots could be active in the same session.
- A previously captured disconnect/recycle panic was traced to the upstream
  hub child-removal race. The project now pins both `usb` and `usb_host_msc` to
  fork commit `cc65dc268f9fb6e89b8b3c6c9e94f5aa1dbb2ccb`, which treats only an
  already-absent child (`ESP_ERR_NOT_FOUND`) as a benign recycle outcome.
- No repeat of that USB panic was observed after installing the fixed image.

## Controlled playback isolation

The FLX4 was disconnected before the decisive audio run.

| Run | Observation |
| --- | --- |
| One MP3 deck | 33 seconds stable; zero reported late blocks and underruns; worst mix 4.07 ms, MAIN I2S 9.474 ms, monitor I2S 0.811 ms |
| Two MP3 decks, normal monitor path | Reset after a few seconds; journal classified the first run as WDT but no new coredump was produced |
| Two MP3 decks, monitor transport temporarily disabled | Both decks advanced about 6.5 seconds; zero reported late blocks and underruns; worst mix 3.253 ms and MAIN I2S 9.545 ms; reboot journal then reported `reset=BROWNOUT`, raw core reasons `3/3` |

The final isolation run makes an audio deadline or monitor-task explanation
unlikely. It directly identifies supply collapse as the current blocker.

## Firmware and diagnostics

- Required toolchain: ESP-IDF v6.0.2.
- The full P4 host regression suite passed.
- The monitor-enabled feature image built successfully: 2,459,680 bytes,
  SHA-256 `1777f89932d6f78c26ff964a0edf8aac57e02e07e470bdc648a47dbe4f215162`.
- The temporary monitor-disabled isolation image built successfully:
  2,457,760 bytes, SHA-256
  `3ec3911282bc7fc83a7b83fcd46e83f6b24c0d10c644bea22d13616f581e71cb`.
- Both signed OTA packages verified and transferred successfully.
- The experimental configuration enables bounded flash coredumps and task-WDT
  panic. `/api/status.crash_dump` exposes retained dump metadata.
- The reset journal now records `esp_reset_reason()` plus raw ROM reset reasons
  for both P4 cores.
- Audio output now yields after either 64 continuously busy blocks or 100 ms,
  preventing very slow blocks from postponing the cooperative idle point for
  seconds.

## Important installed/source distinction

At the end of the session the P4 was running the temporary isolation OTA image
in `ota_0`, reporting `RC2-57-gb5d404b-dirty`. That image has monitor transport
disabled solely for diagnosis. The repository source has already restored the
normal MAIN plus monitor/cue path; the next source build will therefore not be
bit-identical to the image currently installed.

## Required retest

Before reconnecting both devices, electrically qualify the common 5 V path:
measure idle/startup/single-deck/dual-deck voltage and current, confirm no
backfeed, verify per-port protection and record the exact cabling. Then build
the restored source, install it, and repeat cold boot, both insertion orders,
controller input/LEDs, storage playback, independent disconnect/reconnect and
a 30-minute dual-active soak. Do not merge until that matrix passes without
WDT, brownout, panic, underrun or audible/UI fault.
