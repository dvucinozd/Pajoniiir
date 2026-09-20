# M2 post-merge record — 2026-09-20

Status: **PASS for the accelerated M2 beta merge and post-merge CI**.

## Source state

- Immutable installed release tag: `M2`
- Tagged/installed source commit:
  `d2dabfa7561ff1e0486acc42c7acf42607654e19`
- Feature completion commit:
  `201151394814c6bf9afd68c02ffcfb7fccd4709b`
- Merge commit on `master`:
  `d3099f9609802a6a0f6c18d660c2ead54c2fe6d0`
- Post-merge CI portability fixes:
  `df981a25626e87fcd304da2b9882bb571e1d3b5f` and
  `c786de7f6fe423331942749b7a651a5e83bcf688`
- Canonical branch after completion: `master`

The merge tree is identical to the completed feature tree. The annotated `M2`
tag was not moved; it continues to identify the exact installed and qualified
beta image.

## Post-merge verification

GitHub Actions run
[`35523213652`](https://github.com/dvucinozd/Pajoniiir/actions/runs/35523213652)
passed on `c786de7f6fe423331942749b7a651a5e83bcf688`:

- complete P4 host regression suite;
- headless LVGL E2E and exact-screenshot gate;
- ESP-IDF v6.0.2 ESP32-P4 firmware build;
- unchanged committed dependency lock;
- resampler no-double-helper check;
- USB DWC decoder wrap-bound check;
- build provenance generation and binary upload.

The two post-merge fixes are build/test portability changes. They do not modify
the installed firmware behavior represented by the immutable `M2` tag.

## Operator production decisions

The device has operated in its current enclosure for approximately two months.
The operator confirmed that this is the intended enclosure configuration and
that an accessible wired recovery path exists. A separate final-enclosure
rerun is therefore waived by explicit operator decision. This is accepted
operational evidence, not a claim that numeric temperature, RF margin or final
rail measurements were captured.

The production service-access policy will use one shared service password. The
risk of a shared credential is explicitly accepted for this product scope;
firmware authenticity remains protected by signed OTA verification.

The operator selected encrypted offline storage for the production OTA signing
key with a separate encrypted offline backup. Neither copy may enter Git or a
release artifact. Provisioning, restricted-access and backup-recovery evidence
remain to be recorded, and the rotation policy is still unresolved. Secure
Boot, Flash Encryption, PMF/WPA3 and SBOM decisions also remain open.

## Remaining production-release boundary

Before an unrestricted production release:

1. close or explicitly accept the remaining security/provisioning decisions;
2. select a new immutable production version without moving `M2`;
3. build, sign, install and smoke the exact production commit;
4. publish its hashes, slot/version evidence and release record.
