# P4 PCM timeline scheduler probe — 2026-09-27

## Scope and image identity

This record closes the isolated R01 scheduler-timing gate for the PCM timeline
publication path. It does not replace the release-image soak, prove acoustic
quality or move the public M2.2 OTA channel.

The probe source was merge commit
`07618a5a7e4d993f0cf0c81ae38b2a61c7bf32f4`. The exact ESP-IDF v6.0.2 build
reported `M2.2-47-g07618a5` and enabled
`CONFIG_AUDIO_PCM_TIMELINE_SCHEDULER_PROBE`. The option is default-OFF and is
not part of the restored maintenance candidate.

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `main-deck-p4.bin` | 2,507,552 bytes | `9acfaca3df97d08d35d85cadd8ee951f9218027fd3a2efbbfc317d5b0433e39c` |
| signed `.ddjota` bundle | 2,507,740 bytes | `f98c7719a49fe3d2939368a7ef2a7decedec43420d1ad9cfeb7eb5455852ea1c` |

The bundle verified against committed release key `rel-001` before upload. It
was installed by the local signed push-OTA service, booted as
`M2.2-47-g07618a5` from `ota_1` and used boot epoch 551.

## Software and build gates

The complete P4 host runner passed on the final implementation, including all
309 PCM timeline assertions and 411 audio-engine assertions. Both local clean
ESP-IDF v6.0.2 builds passed: the normal default-OFF build and the isolated
probe build. GitHub CI passed both host-regression jobs, both P4 build jobs and
the combined host/build job before merge.

The probe starts after `audio_engine_init()`. Its reader notification occurs
after the short publication critical section, so no FreeRTOS wake operation is
performed while the timeline mux is held. PASS requires coherent cursor
publication across forced wrap and handoff cases; the isolated probe does not
exercise the normal `ae_output` playback reader.

## On-device scheduler result

The boot-551 service journal recorded:

```text
seq=6 boot=551 ms=385 level=I event=TIMELINE_SCHEDULER_PROBE a0=100 a1=200 a2=1 a3=0 msg=PASS
```

The fields mean:

- `a0=100`: 100 forced wrap/handoff iterations;
- `a1=200`: 200 reader handoffs completed;
- `a2=1`: maximum measured publication critical section was 1 us;
- `a3=0`: zero coherence, timeout or progress failures.

The measured maximum is below the implementation-plan target of 10 us. The
probe therefore passes its forced-scheduler criterion.

## Paired live dual-deck smoke

Because the isolated reader is not the production output path, the same image
was paired with a live three-minute dual-deck runtime smoke on real media.
Deck 1 loaded track key 115, `House Of Confusion.mp3`; Deck 2 loaded track key
18, `Symphony No.6 (1st movement).flac`. Both tracks and the output remained at
44,100 Hz with the codec open.

After a 20-second stabilization interval, six 30-second checkpoints covered a
180-second measurement window:

| Measurement | Result |
| --- | ---: |
| D1 position advance | 181,853 ms |
| D2 position advance | 181,852 ms |
| UAC submitted-block delta | 31,327 |
| `output_late` delta | 0 / 120 limit |
| PCM underrun delta, D1 / D2 | 0 / 0 |
| UAC underflow / dropped / overflow delta | 0 / 0 / 0 |
| UAC packet-failure / lost-frame delta | 0 / 0 |
| USB daemon / recovery-failure delta | 0 / 0 |
| service-log dropped delta | 0 |
| final UAC ring | `nominal` |
| UAC data-loss flag / flags | `false` / 0 |
| TWDT | not seen |
| version, slot and boot epoch | unchanged |

The first ad-hoc evaluator incorrectly required a fixed 48 kHz output and
therefore wrote a raw `FAIL`. That predicate was invalid: the documented audio
architecture reconfigures PCM5102A I2S1 to the loaded-track rate, and both
loaded tracks were 44.1 kHz. The raw result was preserved and separately
adjudicated PASS after confirming that the codec stayed open and the deck and
output sample rates remained consistently 44,100 Hz at every checkpoint. No
runtime fault was waived.

This smoke is API/counter evidence. No operator listening claim was made for
the temporary probe image. Acoustic acceptance remains tied to the prior
180.058-minute soak of `M2.2-37-g751d3c6`.

## Rollback and conclusion

After the probe and paired smoke, both decks were stopped. The previously
verified rollback bundle for `M2.2-37-g751d3c6` was reverified with key
`rel-001` and installed. The device returned on boot 552 from `ota_0` with OTA
state `idle`, empty `last_error`, mounted storage, active FLX4 profile, MIDI
IN/OUT, UAC and `root_power_mask=3`. PCM underruns, UAC data-loss flags,
packet/drop/overflow failures, USB daemon/recovery failures, service-log drops
and current TWDT state were zero/clear.

The forced scheduler/timing gate is PASS when the isolated probe result and
paired live runtime smoke are considered together. The public M2.2 channel and
immutable release artifacts were not changed. The remaining post-review release
gates are the real Rekordbox cue/loop comparison and duplicate raw track-ID
media isolation. The physical-phone/visible-meter gate passed later on the same
date and is recorded in
[`P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md`](P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md).
