# P4 post-release development plan

Status: **M2.1 released; no open M2.1 release gate**.

## Current baseline

- Production tag: `M2.1`
- Frozen source: `70824d24dbb1c8d72d19f15797afa2946c5eb909`
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

## Ordered future work

These are optional post-M2.1 projects, not defects in the released product:

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
