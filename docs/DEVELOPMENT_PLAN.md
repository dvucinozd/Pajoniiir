# P4 post-release development plan

Status: **M2.2 released; post-M2.2 maintenance planning**.

## Current baseline

- Production tag: `M2.2`
- Frozen source: `2c2ec32c253d368765123d7bbf8d37389b790b55`
- Required toolchain: ESP-IDF v6.0.2
- Active firmware target: `firmware/main-deck-p4`
- Current architecture: direct P4 USB0 media plus USB1 FLX4 MIDI/UAC

The release qualification, lifecycle matrix, mixed-format playback, final
combined soak, OTA fault paths and exact-image product smoke are complete.
Do not reopen a closed gate without a reproducible failure, a changed
assumption or an explicit new release requirement.

## Maintenance rule

Every change starts from its impact, not from the age of the previous test:

| Change area | Minimum evidence before release |
| --- | --- |
| Documentation/media only | `tools/check_documentation.ps1`, `git diff --check`, Documentation integrity CI and Pages review when `index.html` changes |
| Host-only tooling/test | affected suite plus complete P4 host suite when shared behavior changes |
| Firmware logic | complete P4 host suite and ESP-IDF v6.0.2 build |
| UI | UI simulator E2E plus firmware build; physical display/touch smoke for release |
| USB/power/audio/OTA | relevant focused hardware regression plus exact-image product smoke |
| DSP, scheduler or buffering | focused fault/counter test and an appropriately long audible hardware run |
| Partition, bootloader or trust key | wired recovery plan, isolated signed build and supervised hardware installation |

Preserve firmware version, source commit, slot, boot identity, strict counter
deltas and operator-visible/audible results in new validation records.

## Completed M2.2 scope

M2.2 is a focused Wi-Fi Remote upgrade. It preserves the M2.1 USB, playback,
audio, OTA and controller topology while replacing the embedded HTML/CSS/JS
operator surface with a responsive dual-deck console.

The implementation contract is:

- the page remains fully embedded and has no runtime CDN, webfont or framework
  dependency;
- P4 `deck_core` remains authoritative for playback and SYNC state;
- continuous browser gestures may preview locally, but decoder seek is committed
  once per completed gesture;
- every mutation remains a marked POST request and must handle non-2xx responses;
- service cards expose existing signed OTA, network and controller-profile APIs
  without weakening their confirmations or credential boundaries;
- desktop, phone landscape and phone portrait layouts receive automated contract
  coverage plus visual review;
- release acceptance requires the complete P4 host suite, ESP-IDF v6.0.2 build,
  OTA installation and a focused physical Wi-Fi Remote playback/seek/mixer/OTA
  smoke on the exact candidate image.

All implementation gates above passed on commit `2c2ec32`. The exact `M2.2`
tag was built, signed, installed on `ota_0`, hardware/API-smoked and published
through both the public pull channel and GitHub Releases. The release record is
[`validation/M2_2_PRODUCTION_RELEASE_20260923.md`](validation/M2_2_PRODUCTION_RELEASE_20260923.md).

## Ordered future work

These are optional post-M2.2 projects, not defects in the released product:

1. **Non-FLX4 controller qualification.** Obtain real hardware and capture
   descriptors, MIDI, LEDs, reconnect behavior and four-channel audio before
   advertising any additional controller profile.
2. **Recorder qualification.** Keep `CONFIG_AUDIO_RECORDER_ENABLED` disabled
   until SD latency, removal, capacity, finalize/power-loss and combined audio
   load are physically qualified.
3. **Security hardening pilot.** Before a later production batch, qualify
   Secure Boot v2, release-mode Flash Encryption, eFuse provisioning and full
   wired recovery on a sacrificial P4 board.
4. **Service credentials.** Consider per-device credentials and
   WPA3-only/PMF-required mode if deployment expands beyond the current
   controlled environment.
5. **Signing-key rotation.** Verify the offline backup through a disposable
   signature test, then implement successor-key overlap before retiring
   `rel-001`.
6. **LIBAPTA integration.** Begin only after the upstream embedded profile and
   release gates in [`LIBAPTA_P4_INTEGRATION_PLAN.md`](LIBAPTA_P4_INTEGRATION_PLAN.md)
   are satisfied.

## Release discipline

- Never move published tags.
- Build a release from the exact clean commit that will be tagged.
- Run automated gates before tagging and exact-image hardware acceptance before
  publication.
- Publish immutable versioned OTA content before switching `latest.json`.
- Keep signing keys and hosting credentials outside Git and release artifacts.
- Retain wired recovery until any new hardware-rooted security configuration
  has been independently proven.
