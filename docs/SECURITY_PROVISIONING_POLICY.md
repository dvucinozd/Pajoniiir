# P4 security and provisioning policy

Status: **applied to published JC4880/FLX4 v91; reconciled 2026-10-06**.
Irreversible ESP32-P4 provisioning remains intentionally deferred.

## Release identity

- Current production is `M2.4-91-g75136aef`, frozen at
  `75136aef749a1f03d9089c8b6ff6452b3dba0839`. Its tag/assets are immutable;
  later diagnostic/docs commits are not the accepted image. See the
  [release record](validation/JC4880_V91_RELEASE_20261005.md).
- The existing annotated `M2` tag is immutable and must not be moved.
- The existing annotated `M2.1` tag remains the immutable prior production
  baseline and must not be moved.
- The annotated `M2.2` tag was created after pre-tag automated gates, and its
  exact tagged image passed installation and product smoke before the tag and
  production channel were published. The published tag is immutable and must
  never be moved.
- The annotated `M2.3` and `M2.4` tags are immutable. M2.4 supersedes M2.3
  because the latter could not validate the current valid cross-signed public
  TLS chain.
- Pull OTA orders comparable versions and offers only strictly newer signed
  images; equal v91 does not offer a duplicate. Local signed push is the
  intentional rollback path. Project identity prevents cross-board activation;
  JC1060 hardware and public channel publication remain NOT RUN.

## Service network

- The product uses the accepted shared service credential. Do not publish it
  in release notes or artifacts beyond the firmware configuration that must
  contain the SoftAP credential.
- SoftAP policy is WPA2/WPA3 transition mode with PMF capability enabled.
  `required=false` is deliberate so existing WPA2 service clients can still
  connect; WPA3 clients negotiate SAE/PMF.
- Signed `.ddjota` verification remains mandatory. Wi-Fi association alone
  never authorizes unsigned firmware.
- Revisit per-device credentials and WPA3-only/PMF-required mode before any
  wider or unattended deployment.

## OTA signing key

- `rel-001` remains the trusted release key for v91, unchanged from M2.4.
- Store the private key in encrypted offline primary storage and keep one
  separately located encrypted offline backup. Neither copy may enter Git,
  build logs, release artifacts, firmware/NVS or the hosting account.
- The operator confirmed creation of both encrypted offline copies on
  2026-09-20 and explicitly accepted backup recovery signing as deferred
  maintenance rather than an open M2.4 gate.
- Verify backup recovery by signing a disposable test payload which the
  committed public key accepts. Record only success, key ID and public-key
  fingerprint; never record the private key or its passphrase. This is a future
  maintenance action and is not claimed as completed v91 evidence.
- Normal rotation is a firmware release signed by `rel-001` that introduces a
  successor trust key before `rel-001` is retired. Emergency recovery after
  loss or compromise uses the confirmed wired service path. Multi-key overlap
  is future implementation work and is not claimed by v91.

## Secure Boot, Flash Encryption and eFuses

The v91 release does **not** enable ESP32-P4 Secure Boot, Flash Encryption or security
eFuse provisioning. There is only one ESP32-P4 board, and the currently
confirmed wired recovery path has not been qualified after those irreversible
changes. Burning security eFuses on the only working unit is outside this
release scope.

This is an explicit accepted limitation: a party with physical access can
replace or read flash despite signed application OTA. Operational controls are
the closed enclosure, controlled physical access, signed OTA, encrypted
offline signing-key custody and the wired recovery path.

If a later production batch requires hardware-rooted protection:

1. qualify the complete recovery/manufacturing process on a dedicated pilot
   board that may be sacrificed;
2. use ESP32-P4 Secure Boot v2 with RSA-PSS, not ECDSA;
3. provision Flash Encryption in release mode before enabling Secure Boot;
4. verify signed/encrypted boot, both OTA slots, rollback policy and recovery;
5. audit every planned eFuse value and burn command, then require an
   independent operator review before the irreversible step;
6. preserve the provisioning log without private key material.

No release script may burn an eFuse implicitly.

## SBOM decision

The inherited no-SBOM release policy is unchanged for v91. Dependency provenance and the
committed ESP-IDF component lock remain mandatory, but no SPDX/CycloneDX
generator is a release gate unless the distribution or compliance scope
changes.
