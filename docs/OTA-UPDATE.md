# Pajoniiir OTA Update Procedure

Status on `feat/p4-dual-usb-host`: P4 is the only active OTA target. Signed
dual-slot P4 OTA and rollback retain their hardware acceptance. The former
dual-target `.ddjota` implementation, valid A/B updates, rejection matrix,
interrupted uploads and forced rollback passed on 2026-07-14 with release
`RC1-123-g587cd7a1`; that S3 evidence is historical and is not an active release
instruction. See
[`OTA_UPDATE_PLAN.md`](OTA_UPDATE_PLAN.md) for the design and acceptance record.
The latest matching rollout, `RC1-131-gc391e306`, was signed, independently
verified and installed on 2026-07-16: P4 finished on `ota_1`, S3 on
`ota_0 / valid`. This rollout proved package/install/boot health but intentionally
did not repeat the complete functional hardware smoke.

The latest P4-only deployment was `RC2-51-g050ab43` on 2026-08-22. The local
push endpoint accepted the signed bundle, and COM15 later confirmed P4
`ota_0 / valid`, a mounted 29,520 MB SDHC card and the still-running S3 at
`RC2-44-g1923a3b / ota_1 / valid`. The USB medium exhausted eight automatic
enumeration-recovery cycles after the restart and required one physical
reinsert before its exFAT volume and 324-track library loaded. This is positive
P4 OTA and reconnect evidence, not a matching dual-target rollout or complete
reboot-recovery pass. See
[`validation/RC2_51_P4_OTA_DEPLOYMENT_20260822.md`](validation/RC2_51_P4_OTA_DEPLOYMENT_20260822.md).

## Safety rules

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
- Back up the private key offline and restrict access. Losing it prevents
  future OTA releases unless a new trust key is installed over a wired path.
- The current PEM is an unencrypted development/release key. Before production
  distribution, move signing to encrypted offline storage, a secret store or
  hardware-backed signer and define a key-rotation procedure.

The current firmware trusts one key ID, `rel-001`. Adding or replacing trusted
keys requires a firmware update signed by the existing key or a wired recovery
flash. The remote pull channel accepts only a newer monotonic Pajoniiir
`RC<tag>-<commits>-g<hash>` version. Older signed releases remain installable
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

## Build the P4 target

Initialize ESP-IDF and use an isolated release build so stale ignored
`sdkconfig` files cannot silently select an old partition layout:

ESP-IDF **6.0.2 is required**, not merely recommended:
`firmware/main-deck-p4/main/idf_component.yml` pins `idf: "==6.0.2"`, so an older
environment fails during dependency resolution rather than producing a
questionable image. The 5.5 environments below are no longer usable for this
tree.

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
idf.py --version   # must report ESP-IDF v6.0.2
$repoRoot = git rev-parse --show-toplevel

Set-Location "$repoRoot\firmware\main-deck-p4"
idf.py -B build_signed fullclean
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build
```

Do not package unless the P4 build exits with code 0 and fits its 4 MiB slot.

The latest clean-build evidence, including raw-image sizes and SHA-256 values,
is recorded in
[`validation/CLEAN_RELEASE_RC2_BUILD.md`](validation/CLEAN_RELEASE_RC2_BUILD.md).
Both signed RC2 application packages were installed successfully through OTA
on P4 and S3 on 2026-08-02 and both targets reported `RC2`. Complete ESP-IDF
v6.0.2 boot chains were installed afterwards over the wired recovery ports,
because application OTA does not replace bootloaders or partition tables. See
[`validation/RC2_FOCUSED_FUNCTIONAL_SMOKE_20260802.md`](validation/RC2_FOCUSED_FUNCTIONAL_SMOKE_20260802.md).
The later `RC2-51-g050ab43` clean dual-target development build and P4-only OTA
deployment are recorded in
[`validation/RC2_51_P4_OTA_DEPLOYMENT_20260822.md`](validation/RC2_51_P4_OTA_DEPLOYMENT_20260822.md).
The superseded ESP-IDF 5.5.4 record is
[`validation/CLEAN_RELEASE_RC1_259_BUILD.md`](validation/CLEAN_RELEASE_RC1_259_BUILD.md).

### Version strings

The application version comes from `git describe`, so it is `RC<tag>` at a
tagged commit and `RC<tag>-<distance>-g<hash>` afterwards. The prefix moved from
`RC1` to `RC2` on 2026-07-30 to mark the ESP-IDF 6.0.2 baseline. Pull OTA orders
releases on the tag number first, so any `RC2*` is newer than every `RC1*`.

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
2. Connect to `Pajoniiir` using the default WPA2 password `Pajoniiir`, then
   open `http://192.168.4.1`.
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

## Historical S3 OTA path

The S3 Debug AP, maintenance token, `/api/ota/s3` endpoint and S3 bundle were
retired from the active product on `feat/p4-dual-usb-host`. Their accepted
behavior remains documented in the dated validation records and Git history;
do not build, package or deploy an S3 image as part of a P4-only release.

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

The unsigned rollback baseline accepted on 2026-07-13 was
`RC1-106-g717b6ab3`, with both targets at `ota_0 / valid`. The first signed
hardware smoke used `RC1-108-g1be328a9-dirty`: P4 completed
`factory -> ota_0 -> ota_1`, S3 completed `ota_0 -> ota_1 -> ota_0`, both
targets rejected modified signed fields, wrong targets and modified image data,
and final status was P4 `ota_1` plus S3 `ota_0 / valid`.

The complete signed E1 acceptance on 2026-07-14 used clean release
`RC1-123-g587cd7a1` and key ID `rel-001`. P4 updated from
`factory / RC1-121-gb7ac66a5` to `ota_0`; S3 updated from
`ota_0 / RC1-121-gb7ac66a5` to `ota_1`. Both targets rejected a wrong signing
key (HTTP 403), wrong key ID, chip/project mismatch and truncated/extended
bundles (HTTP 400) without changing the active slot. A client disconnect after
128 KiB left each current release bootable. Signed `ROLLBACK-TEST-P4-123` and
`ROLLBACK-TEST-S3-123` images restarted before confirmation and were rolled back
to P4 `ota_0` and S3 `ota_1`. Final UI/touch, dual-deck playback and scratch,
FLX4 controls/LEDs, MAIN and headphone-cue smoke passed. The private `rel-001`
key has an offline USB backup; production key rotation remains future work.

The non-destructive 2026-07-16 rollout used clean release
`RC1-131-gc391e306`, key ID `rel-001`, signed P4/S3 bundles and a signed outer
manifest. P4 updated `ota_0 / RC1-126-g812ad70f -> ota_1 /
RC1-131-gc391e306`; S3 updated `ota_1 / RC1-123-g587cd7a1 -> ota_0 / valid /
RC1-131-gc391e306`. Both uploads returned HTTP 200, P4 status remained stable,
and the P4-visible periodic S3 report confirmed the matching valid S3 image.
Exact artifact sizes, hashes and deferred checks are recorded in
[`validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md`](validation/SIGNED_OTA_RC1_131_DEPLOYMENT.md).

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
