# P4 service-network association retry — 2026-09-24

Status: **bounded retry implemented; exact image installed; AP -> STA -> AP
probe PASS; loaded-deck pull OTA retest pending**.

## Reproduced boundary

The installed `M2.2-5-gac0da97` image twice rejected a staging pull before the
bundle request with `could not join network`. Both decks were loaded but stopped.
The staging origin saw no bundle GET, so audio stop, flash and bundle verification
were not reached.

A later read-only network inventory showed two `ZAKLJUCANO` BSSIDs at full
signal. An explicit connectivity probe then completed AP -> STA -> AP, received
`192.168.0.241`, and reported `round trip complete`. A following canonical HTTPS
channel check also completed and correctly rejected public `M2.2` as older than
the running review image. Stored credentials, DHCP and HTTPS therefore work;
the failure boundary is a transient association result.

## Remediation

Commit `5275c9b` changes `wifi_link_switch_to_sta()` from one association attempt
to at most three attempts with 1 s and 2 s backoff. The original caller timeout
remains the hard upper bound, so a wrong password or missing DHCP server still
returns the deck to its own AP in bounded time. Each disconnect records and logs
the Wi-Fi reason code without exposing credentials.

The existing pure `wifi_link_retry` policy remains the single finite retry
authority. The host runner now locks its use in the temporary STA path together
with the disconnect reason and total-timeout guard.

## Verification before hardware install

- `tests/run_p4_host_tests.ps1`: PASS, including the new STA retry contract and
  the existing finite retry unit tests.
- ESP-IDF `v6.0.2` incremental build: PASS, 2.505.648 B, 1.164.368 B remaining.
- Exact-commit full-clean `build_signed`: PASS for `M2.2-8-g5275c9b`.
- Application: 2.505.696 B, SHA-256
  `57fde2bf9847bd870e19bc24996987d35277803ccfbfdebb0850bb480496dd51`.
- Signed bundle: 2.505.884 B, SHA-256
  `cc2f50175210149873567ee9d345282174ddc24d26402f7a74c857a83f64ede7`,
  ECDSA P-256/SHA-256 key `rel-001`; independent package verification PASS.
- Signed local push OTA: PASS, `M2.2-5-gac0da97` on `ota_1` to
  `M2.2-8-g5275c9b` on `ota_0`; firmware state `idle`, empty `last_error`.
- Exact-image AP -> STA -> AP probe: PASS, service address `192.168.0.241`,
  `round trip complete`.

The public OTA channel and immutable `M2.2` release were not changed.
