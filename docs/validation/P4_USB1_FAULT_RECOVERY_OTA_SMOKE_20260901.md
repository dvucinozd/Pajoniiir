# P4 USB1 bounded fault recovery and OTA smoke — 2026-09-01

This record covers the controller USB recovery-storm remediation on
`feat/p4-dual-usb-host` and the exact committed signed candidate that was
installed afterward.

Verdict: **PASS for the bounded-recovery regression, signed OTA boot, idle
stability, dual-deck direct-UAC playback and one USB1 DDJ-FLX4
disconnect/reconnect cycle while USB0 storage remained mounted.** This is a
focused smoke, not the repeated reconnect, 30-minute combined-load or
electrical/VBUS qualification.

## Source and build provenance

- Branch: `feat/p4-dual-usb-host`.
- Commit: `269036b72b471778ecc00cd0fcad58f26aa9f454`
  (`fix(p4): bound controller USB fault recovery`).
- Required and used SDK: ESP-IDF v6.0.2.
- Reported application version: `RC2-109-g269036b`.
- Clean build procedure: `idf.py -B build_signed fullclean`, followed by
  `idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build`.
- Application size: 2,450,656 bytes.
- Application SHA-256:
  `7776f287f9f795abeee36ac648dc518517ec270823928293bf0b04cb334cd9ee`.
- Remaining product binary budget: 1,219,360 bytes; the 4 MiB app partition
  retained 42% free.
- Signed bundle: `main-deck-p4.ddjota`, ECDSA P-256/SHA-256 key ID `rel-001`.
- Bundle SHA-256:
  `29bf0bc173104d4a495897d176149304dbdf01f365ab9931e28bd62cab90c7d3`.

The packager and independent `verify-bundle` and `verify-file` commands all
completed successfully. Generated release artifacts remain ignored under
`releases/pajoniiir-RC2-109-g269036b/`.

Before the exact-commit build, the complete P4 host suite passed with the new
15-check controller recovery-gate test, and an ESP-IDF v6.0.2 development
build passed. No source change occurred between that test run and commit
`269036b`; the clean exact-commit build above is the release-traceable build.

## Fault and remediation

The previously installed development firmware could repeatedly request USB1
root recovery after a MIDI submit/resubmit or transfer fault while the FLX4 was
still attached. The manager correctly suppressed most requests, but the
controller produced roughly 100 requests per second and accumulated 56,068
controller requests in the observed boot. A separate direct-UAC fault could
leave the ring full and the consumer stopped while status still appeared
active.

Commit `269036b` changes the controller fault lifecycle so that:

- the first fault opens one recovery epoch and subsequent reports in that
  epoch are coalesced;
- MIDI OUT acceptance stops and the UAC owner is asked to stop;
- active MIDI endpoints are halted/flushed and all callbacks retire before the
  device/interface ownership is released;
- at most one USB1 recovery request is submitted after teardown;
- a physical `DEV_GONE` event cancels the pending soft-recovery request; and
- the controller task observes a latched UAC stream fault and routes it through
  the same bounded teardown.

`/api/status.p4_usb.controller.fault_recovery_epochs` and the software harness
now expose the epoch counter. The pure-C gate test covers first-fault,
duplicate-fault, completion, cancellation and null-argument behavior.

## OTA and boot evidence

The signed bundle was posted to `/api/ota/p4` with the required mutation and
target headers. The device returned HTTP 200 with
`{"ok":true,"rebooting":true}`.

After reboot:

- `/api/firmware` reported `RC2-109-g269036b` in `ota_0`;
- OTA state was `idle` and `last_error` was empty;
- retained service log boot 369 reported `reset=SW` and the same exact version;
- USB0 mounted once and exposed a 100-track Library; and
- USB1 identified the DDJ-FLX4 as `2B73:0045` with the built-in
  `pioneer_ddj_flx4` profile and USB Audio active.

One startup host recovery completed before the measurement window. It did not
come from the controller (`controller.recovery_requests=0`) and did not repeat.

## Idle and playback smoke

During a 20-second idle window after both roots were ready:

| Counter | Delta |
| --- | ---: |
| Host recovery requests / suppression | 0 / 0 |
| Controller recovery requests / fault epochs | 0 / 0 |
| USB daemon errors | 0 |
| UAC dropped blocks / overflow frames | 0 / 0 |
| Free heap | -536 bytes |

Two long MP3 tracks were then loaded by stable Library identity. During the
30-second simultaneous pre-reconnect playback window:

| Observation | Result |
| --- | ---: |
| Deck 1 / Deck 2 progress | 30,081 / 30,082 ms |
| UAC submitted blocks | 5,182 |
| Output late blocks | 0 |
| UAC dropped blocks / overflow frames | 0 / 0 |
| PCM underruns Deck 1 / Deck 2 | 0 / 0 |
| Host/controller recovery requests | 0 / 0 |
| Fault recovery epochs | 0 |
| USB daemon errors | 0 |
| Free heap | -32 bytes |

## USB1 disconnect/reconnect

The operator disconnected only the FLX4, waited approximately five seconds
and reconnected it without touching USB0. Service log boot 369 recorded:

- `CONTROLLER_DISCONNECTED` at 172,233 ms;
- `CONTROLLER_CONNECTED` at 178,804 ms; and
- no reset, panic or USB0 storage event between them.

The post-reconnect snapshot reported three lifetime controller connects and
two disconnects, matching the startup transition plus this one physical
cycle. MIDI OUT acceptance, the FLX4 profile and UAC all returned. USB0 stayed
at one connect, zero disconnects and one successful mount.

The removal edge incremented the fault-epoch diagnostic once, but physical
device-gone handling canceled the pending soft request:
`controller.recovery_requests` remained zero. During the following 15 seconds
there were zero new host/controller recovery requests, suppression events,
fault epochs, daemon errors, UAC drops or overflow frames.

Post-reconnect simultaneous playback then ran for 20 seconds:

| Observation | Result |
| --- | ---: |
| Deck 1 / Deck 2 progress | 20,074 / 20,073 ms |
| UAC submitted blocks | 3,458 |
| Output late blocks | 0 |
| UAC dropped blocks / overflow frames | 0 / 0 |
| PCM underruns Deck 1 / Deck 2 | 0 / 0 |
| Host/controller recovery requests | 0 / 0 |
| Fault recovery epochs / daemon errors | 0 / 0 |
| USB0 mounted / FLX4 connected / UAC active | yes / yes / yes |

The first scripted post-reconnect `play_pause` attempt did not place both
decks in `PLAYING`, so it was excluded. Both states were explicitly normalized
to `READY`, each transition to `PLAYING` was confirmed, and only the subsequent
20-second window above was counted.

## Remaining acceptance gates

- Repeat cold/warm boots and both physical insertion orders.
- Run at least 20 independent USB0/USB1 cycles, including USB0 removal during
  active decode and USB1 reconnect during controller traffic/playback.
- Run the 30-minute dual-active diagnostic soak and later multi-hour product
  soak with heap, task-stack, audio-deadline and UAC data-loss counters.
- Qualify physical MP3, WAV and FLAC fixtures under sustained cache misses.
- Measure the common 5 V rail and protected downstream VBUS on both ports under
  startup and sustained combined load; confirm isolation, current limiting and
  absence of backfeed.

The branch therefore remains outside release/enclosure qualification despite
the focused USB1 recovery pass.
