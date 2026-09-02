# P4 dual-USB runtime smoke — 2026-08-12

## Scope and verdict

This record covers the unfinished `feat/p4-dual-usb-host` bench session. It is
positive evidence for direct P4 dual-root enumeration and for the upstream USB
recycle fix, but it is **not** merge or release acceptance.

Verdict: **blocked by 5 V/VBUS stability**. The most controlled dual-deck run
ended in a hardware-reported brownout even after the FLX4 was disconnected.

An additional retained-trace run later the same day ruled out the initially
suspected audio-task deadlock. The same playback/load workload produced both a
raw P4 `WDT` reset and a direct `BROWNOUT`, with the library transaction already
at `done` in the decisive reproduction. The last audio breadcrumb varied between
normal `main_i2s` and lightweight `snapshot` work and the Task-WDT ISR hook was
never entered. On ESP32-P4 all unhinted core/system MWDT sources collapse to the
generic `ESP_RST_WDT`, so that code alone does not identify a Task WDT. The
combined evidence is consistent with the unqualified 5 V/VBUS path disturbing
the P4/USB subsystem, not with one deterministic audio function blocking.

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
| One playing deck plus serialized metadata loads | Six loads completed in 92–219 ms; the seventh also reached retained `done`, then the board reset about 131 ms later with raw `WDT`; Task-WDT hook remained false |
| Fine-trace repeat after OTA | First metadata load completed in 145 ms, then the board reset about 100 ms later with raw `BROWNOUT`; retained audio phase was normal blocking MAIN I2S output |

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
- A two-slot `.noinit` audio journal records block/phase/subphase, active decks,
  last idle time and whether the Task-WDT ISR hook ran. A second retained journal
  covers the library/cache transaction down to SD/USB gate waits and individual
  DAT/EXT signatures; both are exposed by `/api/status` and copied into the boot
  service journal after an unexpected reset.
- ANLZ cache writes are disabled only in the experimental P4-local-controller
  profile. Existing cache reads and USB parsing remain available. A temporary
  one-second catalog pacing experiment was removed because a single isolated
  cold load could still reset the board and the delay did not address the rail.

## Important installed/source distinction

At the end of the session the P4 was running the normal monitor-enabled,
fine-grained retained-trace image in `ota_1`, reporting
`RC2-58-g008cfa4-dirty`. Its application payload is 2,465,424 bytes with SHA-256
`b1993a41b6a6ac78f4a26e57f9d63691a8fb2d3b205adfe96ee5e89ba8672e6d`.
The board was left idle after the final brownout reproduction. This is a
diagnostic experimental image, not a release candidate.

## Required retest

Before any further reset stress or reconnecting both devices, electrically
qualify the common 5 V path:
measure idle/startup/single-deck/dual-deck voltage and current, confirm no
backfeed, verify per-port protection and record the exact cabling. Then build
the restored source, install it, and repeat cold boot, both insertion orders,
controller input/LEDs, storage playback, independent disconnect/reconnect and
a 30-minute dual-active soak. Do not merge until that matrix passes without
WDT, brownout, panic, underrun or audible/UI fault.
