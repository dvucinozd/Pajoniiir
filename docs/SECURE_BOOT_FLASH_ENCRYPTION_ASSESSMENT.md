# Secure Boot and Flash Encryption Assessment

Status: **P4-only decision pending, updated 2026-09-09**. Neither feature is
enabled in the active firmware.

Secure Boot v2 and flash encryption are production-hardening decisions, not a
late OTA-only switch. They require a dedicated ESP-IDF 6.0.2 build, partition
and bootloader size verification, wired installation planning, key custody and
documented recovery behavior.

The current P4 partition table places data at `0x9000` and the first app at
`0x20000`. Any bootloader or partition-offset change requires a full wired
flash; an application-only OTA cannot migrate the table. NVS migration or
intentional reset must also be planned if data offsets move.

Before enabling either feature:

1. build the exact release configuration with Secure Boot v2 enabled and prove
   the bootloader fits its allocated region;
2. build with the intended flash-encryption mode and verify every app/OTA slot;
3. define signing/encryption key generation, offline storage, backup and access;
4. verify factory recovery, both OTA slots, rollback and service procedures on
   sacrificial hardware;
5. document irreversible eFuse operations and require a second-person review;
6. repeat signed OTA, USB, audio and long-soak acceptance on the hardened image.

Do not burn production eFuses during ordinary development. Until the decision
and sacrificial-device validation are complete, keep this risk open in
[`RISK_REGISTER.md`](RISK_REGISTER.md).

The earlier dual-target assessment is retained only as historical evidence in
[`ARCHIVE_SECURE_BOOT_FLASH_ENCRYPTION_ASSESSMENT_DUAL_TARGET.md`](ARCHIVE_SECURE_BOOT_FLASH_ENCRYPTION_ASSESSMENT_DUAL_TARGET.md).
