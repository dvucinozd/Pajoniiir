# Clean Release Build — RC2

Date: 2026-07-30
Source: `RC2` (`56905c89`)
Environment: ESP-IDF v6.0.2, Windows PowerShell

## Why the version prefix changed

`RC1` tagged the ESP-IDF 5.5.4 line. `master` now requires ESP-IDF 6.0.2 —
`firmware/*/main/idf_component.yml` pins `idf: "==6.0.2"`, and the 5.5
compatibility code (the private DWC HAL wrapper, the `ESP_IDF_VERSION=5.5`
override) has been removed. That is a different build baseline, so it gets a new
release prefix.

The annotated tag `RC2` was created on `56905c89` and pushed. Application
version comes from `git describe` (ESP-IDF default), so a build at exactly that
commit reports the bare string `RC2`; every later commit reports
`RC2-<distance>-g<hash>`.

A bare `RC2` was checked against the code that consumes it before tagging:

- `parse_release_version()` in `firmware/main-deck-p4/components/p4_ota_pull_core/p4_ota_pull_manifest.c`
  has an explicit branch for a tag with no distance suffix, so `RC2` parses.
- `p4_ota_pull_release_compare()` orders on the tag number first, so `RC2`
  (tag 2) is NEWER than any `RC1-*` (tag 1). Pull OTA from the RC1 firmware on
  the bench will accept it.
- `tools/package_ota_release.ps1` reads `project_version` out of
  `project_description.json` and does not parse the string, so the missing
  `-g<hash>` suffix does not affect packaging.

One consequence worth stating: a build at the tagged commit carries no commit
hash in its version. Later builds do. If a bare-`RC2` image is ever flashed,
`56905c89` is the only commit it can have come from.

## Scope

Both firmware targets were rebuilt from clean isolated `build_signed`
directories on a clean working tree. Each CMake configuration reported the same
application version: `RC2`, confirmed in `project_description.json` for both
targets rather than inferred from the tag.

Commands:

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1

Set-Location firmware\control-board-s3
idf.py -B build_signed fullclean
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build

Set-Location ..\main-deck-p4
idf.py -B build_signed fullclean
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build
```

## Raw application images

| Target | Image | Bytes | Hex size | OTA slot | Free bytes | Used | SHA-256 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| S3 | `control-board-s3.bin` | 964,288 | `0xEB6C0` | 1,966,080 (`0x1E0000`) | 1,001,792 | 49.05% | `ee9d205a9f7ef3ff4cc10677fb22a069a8c103c550a5048e23a7e90f6a71952b` |
| P4 | `main-deck-p4.bin` | 2,424,976 | `0x250090` | 4,194,304 (`0x400000`) | 1,769,328 | 57.82% | `4a78ddab5716d003882ef5f4204520278002c20d33328135540036b6ab28d0c3` |

Both builds exited 0.

## Observations from this build

- The P4 **bootloader** is at `0x5AF0` with only `0x510` bytes (5%) free in its
  region. The S3 bootloader has `0x2D80` (36%) free. The P4 figure is tight
  enough that a future bootloader-level config change could overflow it; it is
  recorded here so the next overflow is recognised rather than diagnosed.
- The P4 build emitted exactly one compiler warning:
  `components/bsp_jc4880/bsp_jc4880.c:96:33: warning: 's_i2s_tx' defined but not
  used [-Wunused-variable]` — a leftover of the disabled ES8311 path. The S3
  build emitted none.
- `build_signed/sdkconfig` for P4 carries all three pre-v3 silicon selectors
  (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`, `CONFIG_ESP32P4_REV_MIN_100=y`,
  `CONFIG_ESP32P4_REV_MIN_FULL=100`). Without them a clean configuration targets
  revision 3.1 and will not boot the production board, so this was checked
  rather than assumed.

## Boundary

This record proves reproducible clean compilation on ESP-IDF 6.0.2, matching
target versions, chip-specific application images, slot fit, raw-image hashes
and — per the packaging section below — signed `.ddjota` bundles that verify
against the committed public key. It does **not** prove deployment, boot
validity or functional hardware acceptance.

**These images are not release-qualified.** `docs/migration/ESP_IDF_6_0_2_MIGRATION.md`
carries ten open hardware-acceptance rows that this build does not close. A
green build says the code compiles and the host models agree, not that the
silicon behaves.

Relevant to anyone flashing these: the bounded compressed cache streams from USB
continuously, replacing the whole-track PSRAM preload that used to keep playback
off the USB path entirely. The DWC BNA path that panicked under ESP-IDF 5.5 is
therefore permanently exercised now. `usb_dwc_compat_bna_recovered_count()`
exists so a recovery is observable instead of assumed.

## Signed packaging (2026-07-30)

Unlike at `RC1-259`, `keys/ota_signing_private.pem` **is** present on this
development computer, so packaging was performed here with
`tools/package_ota_release.ps1` (key ID `rel-001`, ECDSA-P256-SHA256). Output in
the ignored `releases/pajoniiir-RC2/`:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `main-deck-p4.ddjota` | 2,425,164 | `36e22e658ed3b1a8b64e301937fc18687e1e6d3d002e7f0f30ff49a17767042b` |
| `control-board-s3.ddjota` | 964,476 | `82748a84c3b5ccf243474968e20463a0c8d78705a188b15ae0437128f7d22d83` |
| `manifest.json` | 1,092 | `b3e5c22bf06846c8010c9af239523a8091931d5abfea7247f7615d8a89344722` |
| `manifest.sig` | 64 | `fc4ab337fb4c18f3fe7d0fe248d8a644a43f119eb2b89c3b2d0aada2ec687021` |

The raw `.bin` hashes inside the bundles match the table above exactly, and the
script's own `verify-bundle` (both targets) and `verify-file` (outer manifest)
checks passed — it throws on failure, so reaching the summary line is the pass.
Both bundles carry `version=RC2`; no version-truncation warning was emitted, so
`RC2` fits the 31-byte ESP application version field with room to spare.

`tools/publish_ota_release.ps1` also generated `latest.json` into the release
directory for the pull-OTA channel:

```json
{ "schema_version": 1, "release": "RC2",
  "p4": { "url": "RC2/main-deck-p4.ddjota", "size": 2425164,
          "sha256": "36e22e6..." } }
```

**Nothing has been uploaded.** That document only takes effect once it and the
bundle are placed on the VPS at `https://pajoniiir.zadar.click/ota/`. Doing so
would make every P4 running an `RC1-*` release see RC2 as NEWER and offer to
install it — which, given the open hardware-acceptance rows above, is a decision
to make deliberately rather than as a side effect of packaging. Wired flash or
local push OTA remains the safer first path for RC2.
