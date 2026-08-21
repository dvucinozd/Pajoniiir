# RC2-51 P4 OTA deployment and media recovery — 2026-08-22

## Scope

This record covers the clean dual-target build and signed packaging of
`RC2-51-g050ab43`, followed by a P4-only local push-OTA deployment. It records
the observed microSD and USB MSC behavior after reboot. It is not a complete
functional smoke, a Hercules hardware acceptance, or an S3 deployment record.

## Build and package provenance

- Source commit: `050ab43665e4398c1fb3610267ef507776ecaf8a`
- Version: `RC2-51-g050ab43`
- Toolchain: ESP-IDF v6.0.2
- Signing key ID: `rel-001` (`ECDSA-P256-SHA256`)
- Both isolated `build_signed` targets completed from `fullclean`.
- The packager verified both `.ddjota` bundles and the signed outer manifest.
- Both committed `dependencies.lock` files remained unchanged.

| Target | Raw image size | Free OTA slot | Raw SHA-256 | Bundle SHA-256 |
| --- | ---: | ---: | --- | --- |
| P4 | 2,433,456 (`0x2521b0`) | 42% | `6277b76fa116617b8a20fa0fc06105aaeff1faf97a16deae72b866c5c87cd464` | `226e0a0973cc944ad93376a6727b56bc388c62cbb2b6794c8cece5e855b58113` |
| S3 | 972,784 (`0xed7f0`) | 51% | `4b0cefdcbf95b5814a4d0c09998126039fba6b6d6e6dd14b0992612643e24c44` | `af345413cc3dbcbd4b5e2b5d730f0736302fd6f0ca06c3d1819322ac51f6c49e` |

The generated release directory was
`releases/pajoniiir-RC2-51-g050ab43/`. Release output is intentionally ignored
by Git. The S3 bundle was built and verified but was not installed in this
session.

## P4 OTA transition

Before upload, `GET /api/firmware` reported:

```text
P4: RC2-46-g2ed6c5b-dirty, ota_1, idle
S3: RC2-44-g1923a3b, ota_1, valid
```

The signed `main-deck-p4.ddjota` was posted to
`http://192.168.4.1/api/ota/p4` with the required target and control headers.
The endpoint returned:

```json
{"ok":true,"rebooting":true}
```

The P4 access point did not become available during the initial 90-second
post-upload probe. The operator then connected the P4 native USB-Serial-JTAG
port as `COM15`. Opening the monitor caused a separate
`CHIP_USB_UART_RESET`; that reset must not be confused with the earlier OTA
restart.

The decoded boot log then confirmed:

```text
ESP-IDF v6.0.2 2nd stage bootloader
running main-deck-p4 RC2-51-g050ab43 slot=ota_0 state=valid
reset reason: 11
```

The image was therefore booted from the opposite OTA slot and had passed the
mandatory startup health confirmation.

## microSD and USB observations

The same COM15 boot mounted the inserted microSD card successfully:

```text
Name: SA32G
Type: SDHC
Speed: 20.00 MHz
Size: 29520MB
SSR: bus_width=4
```

The initial `library_init()` warning occurred before the USB host startup and
is expected. The later USB reconciliation result was not successful:

```text
usb_storage: USB enumeration recovery exhausted 8 fast cycles;
continuing every 30000 ms
```

After the operator physically removed and reinserted the same USB media, the
running firmware recovered without another reboot:

```text
usb_media_mount: exFAT candidate detected at LBA 2048
main: USB media library loaded: 324 tracks
```

The P4 also reported the still-running S3 over the control link as
`RC2-44-g1923a3b`, slot 2 (`ota_1`), state 3 (`valid`). No panic, watchdog or
additional reset appeared during the observed post-mount interval.

## Result and acceptance boundary

- **Pass:** clean ESP-IDF v6.0.2 builds for both targets.
- **Pass:** bundle and outer-manifest signature verification.
- **Pass:** P4 signed local push OTA, opposite-slot boot and `valid` state.
- **Pass:** microSD mount after the recorded COM15 reset.
- **Pass:** live USB disconnect/reconnect recovery and 324-track library load.
- **Observed regression/open:** the already-inserted USB medium did not
  enumerate after the initial OTA restart/recovery sequence; eight fast
  software recovery cycles were exhausted and physical reinsertion was
  required.
- **Not run:** S3 OTA, Hercules physical MIDI/LED/audio checks, track playback,
  dual-deck audio, scratch, Master Tempo, Beat FX, long soak, negative OTA
  matrix and forced rollback.

Keep the complete P4 USB MSC cold-boot/software-reboot/disconnect/reconnect row
open until the post-reboot path succeeds repeatedly without physical access.
