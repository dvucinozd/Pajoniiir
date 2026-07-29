# Secure Boot and flash encryption — readiness assessment

**Status: not enabled, and blocked on a partition-table change. Assessed
2026-07-29; nothing in this document has been applied.**

Neither target configures Secure Boot or flash encryption today —
`sdkconfig.defaults` for both `main-deck-p4` and `control-board-s3` contains no
`CONFIG_SECURE_*` entry at all.

## The blocker: the bootloader has no room

Secure Boot v2 adds signature verification to the bootloader, which grows it by
several KB. There is not that much space.

| | Value |
| --- | --- |
| Bootloader start | `0x2000` (`CONFIG_BOOTLOADER_OFFSET_IN_FLASH`) |
| Partition table offset | `0x8000` (`CONFIG_PARTITION_TABLE_OFFSET`) |
| Bootloader budget | `0x6000` = 24 576 bytes |
| Current `bootloader.bin` | `0x5AF0` = 23 280 bytes |
| **Free** | **1 296 bytes (5 %)** |

Enabling Secure Boot will overflow that region. The build fails loudly rather
than producing a broken image, so this is not a silent hazard — but it does mean
Secure Boot cannot be switched on by flipping a config option.

## What unblocking it costs

`CONFIG_PARTITION_TABLE_OFFSET` has to move up (`0xC000` gives the bootloader
`0xA000` = 40 KB, which is the usual figure for a Secure Boot build). That
shifts `nvs`, `phy_init` and `otadata`, and therefore:

- **A full wired flash is required.** `partitions.csv` already says it:
  *"Install this table with a full wired flash before enclosure; app-only OTA
  cannot migrate a partition table."* Once the enclosure is sealed this becomes
  significantly harder, so the ordering matters — this decision belongs before
  the enclosure, not after.
- **`nvs` moves, so its contents are lost** unless deliberately migrated. That is
  the persisted `app_settings` block: audio output, backlight, time mode, cue
  mode, master trim and the `wifi_remote` toggle. Acceptable if planned,
  surprising if not.

## What is irreversible

Both features burn eFuses and cannot be undone on a given chip:

- **Secure Boot v2** permanently enables signature checking. A board that then
  receives an unsigned or wrongly-signed image will not boot, and there is no
  recovery path through the bootloader.
- **Flash encryption in Release mode** permanently disables plaintext flashing
  over UART. Development mode keeps a limited number of re-flash cycles and is
  the only sane setting until the process is proven.

Combined with the fact that **there is no board available to test on**, and that
a mistake bricks the unit rather than failing a test, this is not work to do
speculatively.

## What already exists, and what these features would add

The OTA path is *already* cryptographically verified: `/api/ota/p4` checks an
ECDSA P-256 manifest signature before erasing flash and the streamed image's
SHA-256 before activation (`ota_manifest`, trusted key `rel-001`). So firmware
delivered over the network cannot be forged today.

Secure Boot closes a different hole — **physical** access. Without it, someone
with the board in hand can flash arbitrary firmware over UART, bypassing the OTA
path entirely. Flash encryption additionally protects data at rest: with the
current build, anyone who can read the flash can extract the contents, including
whatever is in `nvs`.

Whether that matters is a product-scope question. For a device the owner keeps,
the network path is the exposed surface and it is already signed. For units
shipped to third parties, or if anything secret ever lands in `nvs`, physical
protection starts to matter.

## Prerequisites, in order

1. Decide the product scope question above — this is the gate, not a technical
   step.
2. Move `CONFIG_PARTITION_TABLE_OFFSET` to `0xC000` and confirm the Secure Boot
   bootloader fits, with the resulting table committed and CI building it.
3. Plan the `nvs` migration or accept the settings reset, explicitly.
4. Bring up **flash encryption in Development mode first**, on a sacrificial
   board, and keep the re-flash budget in view.
5. Only then consider Release mode and Secure Boot, and only with a documented
   recovery story and the signing key handling already settled — see the
   existing "OTA signing key is lost, copied or cannot be rotated" row in
   `docs/RISK_REGISTER.md`.

All of steps 2–5 need hardware. None of them should be attempted without it.
