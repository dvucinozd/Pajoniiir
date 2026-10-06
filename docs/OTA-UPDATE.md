# Pajoniiir OTA Update Procedure

For the shared-core migration, all three boards have separate project identities:
`main-deck-p4` (JC4880), `main-deck-jc1060` and `main-deck-m3`.
M3 uses its own `/m3` pull root and `M3-*` release ancestry. A tagless clean
development version is `M3-dev-g<12-hex-SHA>`; automatic pull discovery is disabled.
Development packages are signed local migration candidates, not public releases.
Explicit release versions require clean source and at most 31 UTF-8 bytes.

Use `-Project main-deck-m3 -DevelopmentCandidate` to package a clean M3 development
build. Ordinary packaging rejects recorder/UI/storage/Link experiments and OTA
fault injection. Wrong signed project/chip is rejected before `esp_ota_begin`.
Read [the preserving M3 wired migration procedure](M3_SHARED_CORE_MIGRATION.md)
for the one-time project identity transition; the old runtime cannot accept the
new project's OTA bundle. Existing tags/assets/channels remain immutable.

`/api/firmware` reports `running_image_state` separately from OTA transfer state.
An upload returning HTTP 200 does not prove health confirmation: wait for the
expected OTA slot, exact board/source/ELF and IDF state `valid`. The factory
state is reported as `factory`, because it is outside OTA rollback selection.
The requested AP must have actual DHCP readiness and HTTP must be running before
pending confirmation. If Wi-Fi was not requested, it is not a health prerequisite.
Absent FLX4/storage does not trigger rollback. Integrated physical rejection,
interruption and rollback gates still require the new image.

Status: **current board-specific P4 procedure, reconciled 2026-10-06**.
Published JC4880/FLX4 release `M2.5` freezes source
`20f1c3f04a615209ae25e9bbdae649d0f5b44e8d`. The original image, not a rebuild
of later master, is accepted and published. JC1060 has a separate project and
development channel configuration; its hardware/channel publication is NOT RUN.
Both boards run P4; there is no S3 update target.

## Safety rules

Board-specific [L candidates](validation/FORK_IMPROVEMENTS_PACKAGE_L_SOFTWARE_20261005.md)
use `main-deck-p4` for JC4880 and `main-deck-jc1060` for JC1060. Both retain the
existing P4 signing/schema and same physical slot sizes; the fixed build budget
is 0x380000. Packager and runtime reject mismatched signed/embedded project or
version. Ordinary packaging also rejects dirty, preview, recorder, storage experiments
and both OTA startup fault-injection flags.
Specify `-Project main-deck-jc1060` in both packaging/channel tools for that board.
JC1060 uses `https://ota.pajoniiir.eu/jc1060` as its separate configured pull
root; JC4880's existing root/schema is unchanged. Channel generation writes local
files only and does not publish or alter saved device settings. Each candidate's
`candidate-evidence.json` freezes source SHA, exact CI, locks/configuration and
binary/bundle hashes with hardware gates NOT RUN. First JC1060 provisioning
requires wired partition installation; app-only OTA cannot change partitions.

Pending-image startup confirmation: a pending image is confirmed only after
critical core initialization and, if Wi-Fi remote was enabled in saved boot
settings, the AP/HTTP service becoming active within 60 seconds of P4 app
entry. Deadline expiry requests IDF rollback. Accepted/factory images keep
their ordinary startup behaviour. USB devices and connected AP clients are
not required. Physical unconfirmed-image restart rollback and 60.253-second
readiness timeout rejection passed with isolated diagnostics, restoring original
v91. The timeout test injects false readiness while the actual AP/API stay alive;
it does not claim an induced Wi-Fi failure.

- Update P4 only and wait for a clean reboot.
- Upload only `main-deck-p4.ddjota`. Raw `.bin` files are for
  wired recovery and the one-time transition described below.
- Keep power stable and do not play audio during an update.
- Keep wired P4 access until the signed update path has passed on hardware.
- Do not distribute or commit `keys/ota_signing_private.pem`.

The device verifies the embedded ECDSA P-256 signature before flash erase. It
then streams and verifies the signed image size and SHA-256, ESP chip, project
name and signed version before activating the new slot. A package for the wrong
target, a modified manifest, a modified image, an unknown key or trailing data
is rejected.

## First signed-OTA installation

A legacy single-slot image cannot install a partition table through
application OTA. Install the OTA-capable bootloader, partition table, initial
OTA data and application once over USB/UART.

Boards already running the accepted unsigned OTA firmware need one transition
to the signed-OTA firmware. Use a full wired flash whenever possible. The old
unsigned endpoint may alternatively install the new raw application `.bin`
once; after the new firmware boots, every web update must be a `.ddjota` bundle.
Never send a `.ddjota` bundle to the old raw-image endpoint or a raw `.bin` to
the new signed endpoint.

## Signing key custody

- The ignored private key is `keys/ota_signing_private.pem`.
- The trusted public key is committed at
  `firmware/common/ota_manifest/keys/ddj_ota_release_public.der`.
- Production custody policy is encrypted offline primary storage with a
  separate encrypted offline backup. Restrict access to both copies, keep them
  outside Git and release artifacts, and verify backup recovery without
  exposing private key material. Losing both copies prevents future OTA
  releases unless a new trust key is installed over a wired path.
- Encrypted offline primary and separately located backup copies are the
  production custody authority. If an unencrypted working PEM is materialized
  for a supervised release build, keep it outside Git and artifacts, restrict
  access, and remove the working copy after publication verification.

The current firmware trusts one key ID, `rel-001`. M2.4 retains this key.
Adding or replacing trusted
keys requires a firmware update signed by the existing key or a wired recovery
flash. The remote pull channel accepts only a newer monotonic Pajoniiir
`RC<tag>-<commits>-g<hash>`, `M<tag>-<commits>-g<hash>` or
`M<major>.<minor>-<commits>-g<hash>` version. Older signed releases remain installable
intentionally through the local push-OTA service path, preserving controlled
rollback without allowing the unauthenticated discovery document to select it.

## Pull OTA hardening

The normal P4 remote is available at `http://pajoniiir.local` and at the active
AP IPv4 recovery address. API Host validation accepts only those exact
identities (with an optional numeric port); mDNS is discovery, not
authentication, and the existing `X-DDJ-Control: 1` mutation marker remains
mandatory.

`latest.json` is discovery metadata, while authenticity remains in the signed
`.ddjota` manifest. The pull worker nevertheless applies defense in depth:

- only a strictly newer comparable release is offered;
- an offer expires after ten minutes and must then be checked again;
- bundle URLs must be relative paths without traversal, query or fragment;
- advertised size must match the HTTP response and signed bundle layout;
- SHA-256 of the complete downloaded bundle must match `latest.json`;
- the embedded signature and image SHA-256 are still verified before the
  inactive slot is activated.

Use local signed upload when an intentional rollback is required.

## Production pull channel

Current JC4880/FLX4 public release is the original hardware-tested
`M2.4-91-g75136aef` image. Its immutable signed bundle and live channel were
independently downloaded and verified; physical unconfirmed-image rollback
and 60-second startup rejection passed with diagnostic-only images. The
[release record](validation/JC4880_V91_RELEASE_20261005.md) retains the explicit
operator-accepted segmented-soak exception and unrun hardware variants.
JC1060/DDJ-400/real Link peers are not qualified by this publication.

The canonical public channel root is:

```text
https://ota.pajoniiir.eu
```

There is no trailing `/ota` path. A published release must therefore expose:

```text
https://ota.pajoniiir.eu/latest.json
https://ota.pajoniiir.eu/<version>/main-deck-p4.ddjota
```

Generate and verify the channel document with:

```powershell
.\tools\publish_ota_release.ps1 `
  -ReleaseDir .\releases\pajoniiir-<version> `
  -WriteToReleaseDir
```

The hosting account is a deployment secret supplied out of band. Never commit
its username or password, embed either in the public HTTPS URL, place them in a
release artifact or copy them into firmware/NVS. Load credentials only from a
local secret store or interactive credential prompt during publication. Use FTPS or SFTP with host/certificate verification. The v91 publication used
FTPS with CA/hostname validation against the provider certificate; do not
resolve a certificate mismatch by disabling verification or exposing secrets. The public
device-facing channel must remain HTTPS.

Publishing uses the private upload service only to place files. Devices use
the public HTTPS URLs above and never receive the upload credentials. After
upload, verify both public paths, MIME/content length and SHA-256 from a network
that is not connected to the Pajoniiir captive AP before initiating a pull OTA.

## Build the P4 target

Initialize ESP-IDF and use an isolated release build so stale ignored
`sdkconfig` files cannot silently select an old partition layout:

ESP-IDF **6.0.2 is required**, not merely recommended:
`firmware/main-deck-p4/main/idf_component.yml` pins `idf: "==6.0.2"`, so an older
environment fails during dependency resolution rather than producing a
questionable image. The older 5.5 environments are no longer usable for this tree.

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
idf.py --version   # must report ESP-IDF v6.0.2
$repoRoot = git rev-parse --show-toplevel

Set-Location "$repoRoot\firmware\main-deck-p4"
idf.py -B build_signed fullclean
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build
```

Do not package unless the build exits with code 0 and the application is at most
`0x380000` bytes. The physical slots remain `0x400000`; that larger size is not
permission to relax the fixed application budget.

Current frozen-image evidence and its explicit segmented-soak exception are in
the [v91 record](validation/JC4880_V91_RELEASE_20261005.md). Earlier M2.1
idle-continuity/three-hour records retain their own image results.
Application OTA does not replace the bootloader or partition table; use a full
wired flash whenever either changes.

### Version strings

The application version comes from `git describe`. The historical lines use
`RC<tag>` and the beta line begins at the annotated `M2` tag. The selected
production version is `M2.5`; immutable M2.4 and v91 remain available.
A tagged build reports the bare tag; later commits
report `<tag>-<distance>-g<hash>`.

Pull OTA orders the milestone family (`M`) after the historical release-
candidate family (`RC`), then orders milestone major, optional minor and commit
distance. Therefore `M2` is newer than every `RC*`, `M2.1` is newer than every
`M2-<distance>`, `M2.4` is newer than `M2.3`, and a later
`M2.4-<distance>` is newer than bare `M2.4`. The
signed local push path is still required for intentional rollback.

After the exact clean production commit passes pre-tag gates, create the next
immutable annotated tag locally and build/sign/install/smoke that image. Do not push the tag or
publish the channel until acceptance; never move a published tag. Hardware
security and key custody policy is in
[`SECURITY_PROVISIONING_POLICY.md`](SECURITY_PROVISIONING_POLICY.md).

## Create and verify a signed release

From the repository root:

```powershell
$repoRoot = git rev-parse --show-toplevel
Set-Location $repoRoot
.\tools\package_ota_release.ps1
```

The packager requires the initialized ESP-IDF Python environment and the local
private key. It validates the P4 build, signs its bundle and the outer release
manifest, and verifies its own output before succeeding. It creates the ignored
directory `releases\pajoniiir-<version>\` with:

- `main-deck-p4.ddjota` for web OTA;
- raw `main-deck-p4.bin` for wired recovery only;
- `manifest.json` with target metadata, sizes, hashes and signing key ID;
- `manifest.sig`, the ECDSA P-256 signature of `manifest.json`.

Independent verification is available with:

```powershell
python .\tools\ota_signing.py verify-bundle `
  --public-key .\firmware\common\ota_manifest\keys\ddj_ota_release_public.der `
  --input .\releases\pajoniiir-<version>\main-deck-p4.ddjota

python .\tools\ota_signing.py verify-file `
  --public-key .\firmware\common\ota_manifest\keys\ddj_ota_release_public.der `
  --input .\releases\pajoniiir-<version>\manifest.json `
  --signature .\releases\pajoniiir-<version>\manifest.sig
```

## Update P4

1. Enable **Wi-Fi Remote** in P4 Settings.
2. Connect to `Pajoniiir` using the configured shared service password. The current build
   advertises WPA2/WPA3 transition mode with PMF capability. Then open
   `http://192.168.4.1`.
3. Record the running P4 version, slot and state.
4. Select **`main-deck-p4.ddjota`**, confirm and upload.
5. Wait for success and restart; reconnect and refresh `/api/firmware`.
6. Confirm the expected version, opposite OTA slot, empty `last_error` and a
   stable `/api/status` response after mandatory startup. The P4 response's
   top-level `state` is the OTA transfer-service state (`idle` after reboot),
   not the ESP-IDF image state; explicit partition-state evidence comes from
   the `fw_health` boot log. Then check display/touch, USB library, MAIN audio,
   direct USB0 storage, USB1 FLX4 MIDI/audio and controller LED status.

Raw API equivalent:

```powershell
curl.exe -X POST `
  -H "Content-Type: application/octet-stream" `
  -H "X-DDJ-Control: 1" `
  -H "X-DDJ-OTA: p4" `
  --data-binary "@releases\pajoniiir-<version>\main-deck-p4.ddjota" `
  http://192.168.4.1/api/ota/p4
```

## Acceptance and failure behavior

For P4 record project, version, slot and last error. Record image state from
`fw_health`/serial. Do not interpret the HTTP OTA service's
top-level `idle` as an image state. Confirm FLX4 reconnect/control/LED behavior,
PCM5102A MAIN, FLX4 headphone cue and P4 UI/media access.

- Invalid signature/key ID/target/chip/project/version/size or image SHA is
  rejected without selecting the inactive slot.
- A disconnected or interrupted transfer aborts the OTA handle; the current
  slot remains bootable.
- A reset or startup failure before confirmation triggers ESP-IDF rollback.

Current v91 publication, installed-image and automatic recovery evidence are
in the [release record](validation/JC4880_V91_RELEASE_20261005.md).
The [bare M2.4 record](validation/M2_4_PRODUCTION_RELEASE_20260929.md) retains
its original cold-power-cycle observation; it is not a universal requirement
for v91. If expected services do not return, record the fault and verify rollback
before attempting controlled recovery. Do not silently erase failed acceptance.
The retained pull/push recovery matrix is
[`validation/P4_PULL_OTA_FAULT_MATRIX_20260920.md`](validation/P4_PULL_OTA_FAULT_MATRIX_20260920.md).

## Wired recovery

Use wired flashing if the AP is unavailable, neither slot boots, the trust key
must be replaced, or bootloader/partition data is damaged. Flash the complete
ESP-IDF set at the offsets reported by the build:

```powershell
$repoRoot = git rev-parse --show-toplevel
Set-Location "$repoRoot\firmware\main-deck-p4"
idf.py -p <P4-COM-PORT> flash monitor
```

## Release record template

```text
Date/time and operator:
Release version and key ID:
Bundle and manifest signatures verified: yes/no
P4 before -> after slot/version/state:
Wrong-key/tamper rejection tested: yes/no/details
MAIN audio / headphone cue: pass/fail
FLX4 controls/LEDs / USB library/UI: pass/fail
Rollback observed: no/yes, details
Notes:
```
