# Clean Release Build — RC1-259

Date: 2026-07-26
Source: `RC1-259-gdaf4639` (`daf4639`)
Environment: ESP-IDF v5.5.4, Windows PowerShell

## Scope

Both firmware targets were rebuilt from clean isolated `build_signed`
directories before any source or documentation edits in the working tree.
Each CMake configuration reported the same clean application version:
`RC1-259-gdaf4639`.

Commands:

```powershell
. C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1

Set-Location firmware\control-board-s3
idf.py -B build_signed fullclean
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build

Set-Location ..\main-deck-p4
idf.py -B build_signed fullclean
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build
```

## Raw application images

| Target | Image | Bytes | Hex size | OTA slot | Free bytes | SHA-256 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| S3 | `control-board-s3.bin` | 930,704 | `0xE3390` | 1,966,080 (`0x1E0000`) | 1,035,376 | `c36858e3b08af891c85e99c4ce977fddac21f1380a122b12e96e114de79b8001` |
| P4 | `main-deck-p4.bin` | 2,423,408 | `0x24FA70` | 4,194,304 (`0x400000`) | 1,770,896 | `09aeb4903603928666264a6eb9b61c57ea2ed5f3f73a17bdd010996a3eef7b43` |

Both builds completed with exit code 0. The S3 image uses 47.34% of its OTA
slot; the P4 image uses 57.78%.

## Boundary

This record proves reproducible clean compilation, matching target versions,
chip-specific application images, slot fit and raw-image hashes. It does not
prove signing, `.ddjota` packaging, deployment, boot validity or functional
hardware acceptance.

`keys/ota_signing_private.pem` was not present on this development computer,
so no signed bundles or outer manifest were created. The raw images remain in
ignored local `build_signed` directories for transfer to the restricted
key-bearing computer if packaging is required.
