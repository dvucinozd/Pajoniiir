# M2.2 security and provisioning policy

Status: **applied to released M2.2; irreversible ESP32-P4 provisioning
intentionally deferred**.

## Release identity

- The production version is `M2.2`, frozen at
  `2c2ec32c253d368765123d7bbf8d37389b790b55`.
- The existing annotated `M2` tag is immutable and must not be moved.
- The existing annotated `M2.1` tag remains the immutable prior production
  baseline and must not be moved.
- The annotated `M2.2` tag was created after pre-tag automated gates, and its
  exact tagged image passed installation and product smoke before the tag and
  production channel were published. The published tag is immutable and must
  never be moved.
- Pull OTA accepts the bare `M2.2` tag and later
  `M2.2-<distance>-g<hash>` development versions. Local signed push OTA remains
  the intentional rollback path.

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

- `rel-001` remains the trusted release key for M2.2.
- Store the private key in encrypted offline primary storage and keep one
  separately located encrypted offline backup. Neither copy may enter Git,
  build logs, release artifacts, firmware/NVS or the hosting account.
- The operator confirmed creation of both encrypted offline copies on
  2026-09-20 and explicitly accepted backup recovery signing as deferred
  maintenance rather than an open M2.2 gate.
- Verify backup recovery by signing a disposable test payload which the
  committed public key accepts. Record only success, key ID and public-key
  fingerprint; never record the private key or its passphrase. This is a future
  maintenance action and is not claimed as completed M2.2 evidence.
- Normal rotation is a firmware release signed by `rel-001` that introduces a
  successor trust key before `rel-001` is retired. Emergency recovery after
  loss or compromise uses the confirmed wired service path. Multi-key overlap
  is future implementation work and is not claimed by M2.2.

## Secure Boot, Flash Encryption and eFuses

M2.2 does **not** enable ESP32-P4 Secure Boot, Flash Encryption or security
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

An SBOM is explicitly not required for M2.2. Dependency provenance and the
committed ESP-IDF component lock remain mandatory, but no SPDX/CycloneDX
generator is a release gate unless the distribution or compliance scope
changes.
