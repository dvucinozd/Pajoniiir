# P4 exact-image dual-deck seek soak — 2026-09-02

## Result

**PASS for the bounded 30-minute exact-image dual-active diagnostic gate.**

The clean, committed P4 candidate `RC2-111-g4ee76a6` ran two real MP3 tracks
for 1,800 seconds while USB0 storage and the direct USB1 DDJ-FLX4 MIDI/UAC path
remained active. Seven controlled end-of-track avoidance cycles
(`pause` -> seek to zero -> `play`) completed without a reboot, USB loss,
recovery request, audio deadline miss, PCM underrun, UAC block drop or UAC ring
overflow.

This result does not close the electrical qualification, repeated USB0/USB1
reconnect matrix, remove-during-decode, real WAV/FLAC or multi-hour product
soak gates.

## Candidate provenance

| Item | Value |
| --- | --- |
| Repository branch | `feat/p4-dual-usb-host` |
| Source commit | `4ee76a690f58ba4cac90ab10f1a331522e5ae13b` |
| Firmware version | `RC2-111-g4ee76a6` |
| Running OTA slot | `ota_1` |
| ESP-IDF | `v6.0.2` |
| Application size | 2,451,680 bytes |
| Application SHA-256 | `238808c1c0918fbd132387437c70a711176f2ab43fc0b675902834328aa3c006` |
| Signed bundle SHA-256 | `244fa667fab16c7dfe8c1b491e8542d9ee7a2d88ce96cbab172f59cb2639d701` |

Before deployment, the complete P4 host suite and a clean ESP-IDF v6.0.2
firmware build passed. The exact signed image was then installed by OTA and its
version, slot and empty OTA error state were confirmed from the live device.

## Preflight

- USB0 storage was mounted and the Library was available.
- USB1 DDJ-FLX4 was connected with the built-in profile `active`.
- FLX4 MIDI input, MIDI output and direct USB Audio were active.
- The boot epoch was `1` and remained `1` for the complete measured window.
- Deck 1 loaded track key `50`, duration 592,000 ms.
- Deck 2 loaded track key `4`, duration 430,000 ms.

The first web `play_pause` request for Deck 1 returned an accepted response but
the deck remained `READY`. Playback was retried and its state was confirmed
before the timer and counter baselines were started. This intermittent
web/control-event symptom occurred outside the accepted window and remains a
separate follow-up; it was not an audio or USB failure during the soak.

## Procedure and fail-closed criteria

The device was polled every five seconds through `pajoniiir.local`. The run was
failed immediately for any of the following:

- firmware version, OTA slot or boot-epoch change;
- USB0 unmount, controller disconnect, inactive profile or loss of direct UAC;
- output-late or per-deck PCM-underrun delta;
- UAC dropped-block or overflow-frame delta;
- new host/controller recovery, storage disconnect or MIDI disconnect;
- new service-log drop;
- playback stopping outside an intentional restart window.

Twenty seconds before either loaded track's natural end, the harness paused
that deck, sought to zero, restarted playback and confirmed the new state. Both
decks therefore remained under active decode/output load without depending on
EOF behavior.

The lifetime `uac_underflow` value is not used as this gate's data-loss
criterion because it advances while the USB Audio sink emits idle silence. The
bounded UAC acceptance signals for this run were dropped blocks and overflow
frames, both measured as deltas from the preflight baseline.
Consequently the aggregate `/api/status` `data_loss` flag remains latched true
once this lifetime underflow counter is non-zero; it is a diagnostic-semantics
follow-up, not a valid pass/fail verdict for this soak until it becomes
playback-aware or separates idle silence from active starvation.

## Measured outcome

| Measurement | Result |
| --- | ---: |
| Measured duration | 1,800 s |
| Deck 1 controlled restarts | 3 |
| Deck 2 controlled restarts | 4 |
| API retries | 0 |
| Audio output-late delta | 0 |
| Deck 1 PCM-underrun delta | 0 |
| Deck 2 PCM-underrun delta | 0 |
| UAC dropped-block delta | 0 |
| UAC overflow-frame delta | 0 |
| Boot-epoch delta | 0 |
| Host-recovery delta | 0 |
| Controller-recovery delta | 0 |
| Storage-disconnect delta | 0 |
| MIDI-disconnect delta | 0 |
| Service-log dropped delta | 0 |

Final status still reported USB0 mounted, the FLX4 connected with its profile
active, direct USB Audio active, zero host-recovery failures and no OTA error.
Both decks were stopped deliberately after the accepted window.

## Acceptance boundary

This evidence closes the previously open **30-minute dual-active exact-image
diagnostic soak** for this candidate and the present improved bench supply.
It does not establish that the supply or downstream VBUS topology is
electrically safe or production-ready: no rail voltage, startup current,
sustained current, backfeed or current-limit measurements were available.

The next release gates remain:

1. measure the common 5 V rail and independently protected, backfeed-free VBUS
   on USB0 and USB1 under startup and sustained dual-deck load;
2. repeat cold/warm boots, both insertion orders and independent USB0/USB1
   reconnects, including USB0 removal during active load/decode;
3. exercise verified real WAV/FLAC fixtures and direct-UAC 44.1/48 kHz source
   cases with numeric data-loss monitoring;
4. run the later multi-hour product soak and closed-enclosure power/thermal/RF
   acceptance.
