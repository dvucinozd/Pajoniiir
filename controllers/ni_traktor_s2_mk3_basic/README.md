# Traktor Kontrol S2 MK3: basic transport profile

Status: experimental, source-derived, no S2 MK3 hardware acceptance.
This is a limited profile for the existing M2.4 runtime, not complete M2.5
controller support. Do not advertise full S2 MK3 support based on SD installation.

## Source and reproduction

Factory mapping `NI Traktor Kontrol S2 MK3.djayMidiMapping` extracted from
[Algoriddim djay Pro 2.2.9](https://download.algoriddim.com/djay/202010171657/djay_Pro_2.2.9.zip),
member `djay Pro 2.app/Contents/Resources/MIDI Mappings/NI Traktor Kontrol S2 MK3.djayMidiMapping`.
Downloaded 2026-10-01 without installing or executing the application.

- Archive SHA-256: `a3c00db3030859bdb71c74368ac5adcc85d62a06790e31a635fa6c8f663775ee`
- Mapping SHA-256: `abd3519e35ab24235d950ab5940dfdf72cbb6d5342f7d62fba089945cbebfe0e`
- USB ID in mapping: `0x17CC1710` (VID `17CC`, PID `1710`).
- djay channel fields are zero-based; source channels 1/3 become statuses
  `0x91`/`0x93`, not `0x90`/`0x92`. The archive's DDJ-400 map independently
  uses channels 0/1 for its known `0x90`/`0x91` PLAY messages.
- Source mapping has 235 input records, including repeated modifier layers.
  Internal `customClassName` logic is not contained in the plist.

The original user-supplied Traktor TSI uses native control identifiers and is
not the MIDI address source. Original third-party artifacts remain in ignored
`tmp/traktor-s2-mk3/`; only this limited conversion is included here.

From the repository root:

```powershell
python controllers/ni_traktor_s2_mk3_basic/convert_djay.py "tmp/traktor-s2-mk3/NI Traktor Kontrol S2 MK3.djayMidiMapping"
python tools/controller_profile/compile_profile.py controllers/ni_traktor_s2_mk3_basic/profile.json -o controllers/ni_traktor_s2_mk3_basic/profile.s3bin
```

## Included behavior

Both decks: PLAY, CUE, SYNC, headphone PFL, browse encoder press to LOAD,
loop encoder turn to halve/double an already active loop, and loop encoder
press to exit/re-enter the last loop. This does **not** create an automatic
loop when no previous loop exists; use the screen for initial loop creation.

Pads are deliberately fixed to eight hot cues; shifted pad addresses clear
hot cues. HOTCUES/SAMPLES mode buttons and djay modifier layers are not mapped.
Only PLAY/CUE/SYNC/PFL LED outputs are included, using source on=127/off=20.
Pad LEDs, RGB and meters remain unmapped.

## Remaining work

- Mixer and tempo use 7-bit CC values; current semantic mixer/tempo consumers
  expect 14-bit values. Mapping these directly with `cc7_abs` would give the
  wrong range. Add tested scaling/inversion support before enabling them.
- Browser rotation and tempo are marked `flipped` in the source; the current
  profile format cannot encode that inversion.
- Jog relative encoding is documented by the source, but its sensitivity and
  acceleration differ from the FLX4 path. Jog controls remain disabled pending
  proper scaling and touch/bend behavior validation.
- SHIFT routing, pad modes and any initialization performed by djay's custom
  class require further work. No custom-class behavior is guessed here.
- USB audio is disabled in profile metadata. A controller profile alone does
  not generalize the existing FLX4 audio path.
- Confirm USB MIDI enumeration/VID/PID, all button edges, shifted pads, LEDs,
  reconnect/reboot behavior, and eventually MAIN/cue audio on physical S2 MK3.

Enter MIDI mode by holding the left FLX button while connecting USB, with
current controller firmware, per [Native Instruments instructions](https://support.native-instruments.com/support/solutions/articles/69000879412-traktor-switching-your-kontrol-s2-mk3-to-midi-mode).

## Validation and SD installation (2026-10-01)

- Compiler succeeded: S3CP v2, 864 bytes, 46 expanded input entries and 8 LED outputs.
- Checked header version, exact size, CRC, VID/PID, unique input addresses,
  deck channel offsets and absence of unsupported mixer/tempo entries.
- Existing `tests/controller_runtime/run_tests.ps1` passed, including compiled
  profile runtime and FLX4 regression checks. This suite does not physically
  qualify S2 MK3 controls.
- POST `/api/controller-profile` with overwrite disabled returned `ok:true`.
  GET `/api/controller-profiles` confirmed `ni_traktor_s2_mk3_basic`,
  `valid:true`, VID `0x17CC`, PID `0x1710`, size 864.
- The connected FLX4 retained active profile `pioneer_ddj_flx4`; the S2 profile
  is stored on SD, not activated or hardware-tested.
- No firmware changes, firmware build, OTA or device reboot performed.
