# DDJ-FFL4 OTA Update Procedure

Status: signed dual-slot OTA and rollback are hardware-accepted on both targets.
The `.ddjota` implementation, host tests, release builds, valid A/B updates,
full rejection matrix, interrupted uploads and forced rollback passed on
2026-07-14 with release `RC1-123-g587cd7a1`. See
[`OTA_UPDATE_PLAN.md`](OTA_UPDATE_PLAN.md) for the design and acceptance record.

## Safety rules

- Update one processor at a time and wait for a clean reboot.
- Upload only the target's signed `.ddjota` bundle. Raw `.bin` files are for
  wired recovery and the one-time transition described below.
- Keep power stable and do not play audio during an update.
- Keep wired access until both signed update paths have passed on hardware.
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
flash. Signed older releases remain installable intentionally: the project does
not yet enforce anti-rollback because Git-derived version strings are not a
safe monotonic security counter and service rollback remains useful.

## Build both targets

Initialize ESP-IDF and use an isolated release build so stale ignored
`sdkconfig` files cannot silently select an old partition layout:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1

cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build

cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build
```

Do not package unless both commands exit with code 0. P4 must fit its 4 MiB
slot; S3 must fit `0x1e0000` bytes (1.875 MiB).

## Create and verify a signed release

From the repository root:

```powershell
cd D:\Documents\DDJ-FFL4
.\tools\package_ota_release.ps1
```

The packager requires the initialized ESP-IDF Python environment and the local
private key. It validates both builds, signs each bundle and the outer release
manifest, and verifies its own output before succeeding. It creates the ignored
directory `releases\ddj-ffl4-<version>\` with:

- `main-deck-p4.ddjota` and `control-board-s3.ddjota` for web OTA;
- raw `main-deck-p4.bin` and `control-board-s3.bin` for wired recovery only;
- `manifest.json` with target metadata, sizes, hashes and signing key ID;
- `manifest.sig`, the ECDSA P-256 signature of `manifest.json`.

Independent verification is available with:

```powershell
python .\tools\ota_signing.py verify-bundle `
  --public-key .\firmware\common\ota_manifest\keys\ddj_ota_release_public.der `
  .\releases\ddj-ffl4-<version>\main-deck-p4.ddjota

python .\tools\ota_signing.py verify-file `
  --public-key .\firmware\common\ota_manifest\keys\ddj_ota_release_public.der `
  --signature .\releases\ddj-ffl4-<version>\manifest.sig `
  .\releases\ddj-ffl4-<version>\manifest.json
```

## Update P4

1. Enable **Wi-Fi Remote** in P4 Settings.
2. Connect to `PAJONIIR` using the default WPA2 password `PajoNiiiR`, then
   open `http://192.168.4.1`.
3. Record the running P4 version, slot and state.
4. Select **`main-deck-p4.ddjota`**, confirm and upload.
5. Wait for success and restart; reconnect and refresh `/api/firmware`.
6. Confirm the expected version, opposite OTA slot and `valid` state, then
   check display/touch, USB library, MAIN audio, UART and S3 status.

Raw API equivalent:

```powershell
curl.exe -X POST `
  -H "Content-Type: application/octet-stream" `
  -H "X-DDJ-OTA: p4" `
  --data-binary "@releases\ddj-ffl4-<version>\main-deck-p4.ddjota" `
  http://192.168.4.1/api/ota/p4
```

## Update S3

The S3 service AP uses WPA2-PSK and returns to OFF after reboot. Signature
validation remains the firmware-authenticity boundary.

1. Enable **S3 DEBUG AP** in P4 Settings and wait for `ON`.
2. Connect to `PajoNiiiR-S3-DEBUG` using the default WPA2 password
   `PajoNiiiR`, then open
   `http://192.168.4.1/update`.
3. Record the S3 running version, slot and state.
4. Select **`control-board-s3.ddjota`**, confirm and upload.
5. After restart, re-enable the AP, reconnect and inspect `/api/firmware`.
6. Confirm expected version, opposite slot and `valid`, then turn the AP off
   and verify FLX4 controls, LEDs, UART and USB headphone cue.

Raw API equivalent:

```powershell
curl.exe -X POST `
  -H "Content-Type: application/octet-stream" `
  -H "X-DDJ-OTA: s3" `
  --data-binary "@releases\ddj-ffl4-<version>\control-board-s3.ddjota" `
  http://192.168.4.1/api/ota/s3
```

## Acceptance and failure behavior

For both targets record project, version, slot, state and last error. Confirm
FLX4 reconnect/control/LED behavior, PCM5102A MAIN, FLX4 headphone cue and P4
UI/media access.

- Invalid signature/key ID/target/chip/project/version/size or image SHA is
  rejected without selecting the inactive slot.
- A disconnected or interrupted transfer aborts the OTA handle; the current
  slot remains bootable.
- A new image stays `PENDING_VERIFY` until mandatory startup succeeds.
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

## Wired recovery

Use wired flashing if the AP is unavailable, neither slot boots, the trust key
must be replaced, or bootloader/partition data is damaged. Flash the complete
ESP-IDF set at the offsets reported by the build:

```powershell
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -p <P4-COM-PORT> flash monitor

cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py -p <S3-COM-PORT> flash monitor
```

## Release record template

```text
Date/time and operator:
Release version and key ID:
Bundle and manifest signatures verified: yes/no
P4 before -> after slot/version/state:
S3 before -> after slot/version/state:
Wrong-key/tamper rejection tested: yes/no/details
MAIN audio / headphone cue: pass/fail
FLX4 controls/LEDs / USB library/UI: pass/fail
Rollback observed: no/yes, details
Notes:
```
