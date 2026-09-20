# M2 automated release gate — 2026-09-20

Status: **PASS for automated, remote OTA and unattended hardware checks**.
Operator listening, physical FLX4 control smoke and enclosure/production gates
remain separate.

## Immutable source

- Annotated tag: `M2`
- Source commit: `d2dabfa7561ff1e0486acc42c7acf42607654e19`
- Toolchain: ESP-IDF v6.0.2
- Dependency lock: unchanged after fresh resolution and build

The public and installed artifact was built in the canonical checkout. Its
application is 2,459,520 bytes with SHA-256
`4216867d72c4a76f37cc04a5c3b3cf067e08bb9602be8bbd9a8282fe5804dacd`.
The signed bundle is 2,459,708 bytes with SHA-256
`f5620858e9983f8272eceb4d3dc93afee7b906cc6e8335e8280b1ceed5bcf9a5`.

## Fresh-checkout automated gates

An isolated detached worktree was created directly from `M2`; no files from
the feature-branch documentation successor were used. These gates passed:

- complete `tests/run_p4_host_tests.ps1`, including OTA manifest/crypto,
  signing, release-helper, newer-only, offer-TTL and lifecycle/release harness
  coverage;
- headless LVGL navigation and exact screenshot comparison for Overview D1,
  Overview D2, Library, Hot Cues, Settings, screensaver and restored Settings;
- clean ESP-IDF v6.0.2 signed build with 41% of the smallest app partition
  free;
- ECDSA-P256 package creation and bundle verification with key ID `rel-001`.

The fresh worktree application was 2,459,536 bytes with SHA-256
`23092d77eee3c6fd79922eef86ac82e7641aa2d6b969a485939d607b18b776f5`.
It is not the published artifact. ESP-IDF embeds compile time and ELF identity,
so a separate build directory is not expected to reproduce the installed
binary byte-for-byte. Source commit, project/version, sdkconfig and dependency
lock matched; the published and installed hash remains authoritative.

## Interrupted upload recovery

A raw HTTP client declared the complete 2,459,708-byte M2 bundle, delivered the
188-byte signed manifest plus exactly 131,072 image bytes, and then closed the
connection. Firmware reported:

```text
state=failed
last_error=HTTP upload interrupted
received_size=131072
running_version=M2
running_slot=ota_0
boot=14
```

The active slot was not changed. The web API remained reachable, USB0 stayed
mounted and FLX4 profile, MIDI IN/OUT and UAC stayed active without a TWDT or
USB recovery failure.

## Signed rollback and return

Local signed push OTA intentionally installed the older
`RC2-156-gd2dabfa` bridge. HTTP returned 200 and the device booted it on
`ota_1` as boot identity 15. USB0 and FLX4 MIDI/UAC recovered automatically.

The bridge then completed its guarded public channel check, offered `M2`, and
pulled the published signed bundle. The device returned to `M2` on `ota_0` as
boot identity 16 with OTA state `idle`, no error, USB0 mounted, FLX4
profile/MIDI/UAC active, zero PCM/packet/drop/overflow fault counters and no
TWDT. This closes the reduced M2 beta OTA matrix: successful public pull,
interrupted upload recovery and signed opposite-slot rollback.

## Remaining operator gates

No playback was active during the OTA fault sequence. This record therefore
does not replace the final short operator smoke for audible MAIN/cue output and
physical FLX4 controls. It also does not close the production enclosure,
thermal/RF, credential/key-custody or irreversible-security decisions.
