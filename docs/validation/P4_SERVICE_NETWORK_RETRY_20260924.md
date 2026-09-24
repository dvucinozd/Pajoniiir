# P4 service-network association retry — 2026-09-24

Status: **bounded network, HTTP-header and HTTP-body retries implemented;
production-channel pull OTA PASS on controlled retry; exact candidate installed
and functional smoke PASS; Wi-Fi OFF/ON ESP-Hosted panic reproduced and fixed
in a successor candidate; one earlier controller watchdog remains an open
intermittent finding**.

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

## Slow HTTPS body boundary

The first loaded-deck staging pull reached the temporary HTTPS origin and the
origin returned HTTP 200 for the complete bundle. The device then failed at
`read signed header: ESP_ERR_INVALID_RESPONSE` before opening the OTA
partition. ESP-IDF 6.0.2 returns `-ESP_ERR_HTTP_EAGAIN` when a transport read
times out before the next body bytes arrive; the pull client incorrectly
treated that temporary idle window as EOF.

Commit `7e2cc69` adds at most three consecutive body-read retries. The retry is
restricted to `-ESP_ERR_HTTP_EAGAIN`; a zero-byte read remains EOF and still
rejects a truncated header or image. Exhausting the idle budget reports
`ESP_ERR_TIMEOUT`, and any received bytes reset the consecutive-idle budget.

- Full P4 host suite: PASS, including the transport-idle/EOF static contract.
- ESP-IDF `v6.0.2` full-clean `build_signed`: PASS for
  `M2.2-10-g7e2cc69`.
- Application: 2.506.208 B, SHA-256
  `3b201b6314dc818fa43ac4419bbea36e4bc397eaf85f5299f99f4ef5e91c046b`.
- Signed bundle: 2.506.396 B, SHA-256
  `9b00b911cc684557e2cff6f498c657623ba6e40e80c7eff3a2d4d1f6e0882ce1`,
  ECDSA P-256/SHA-256 key `rel-001`; independent package verification PASS.
- Signed local push OTA: PASS, `M2.2-8-g5275c9b` on `ota_0` to
  `M2.2-10-g7e2cc69` on `ota_1`; firmware state `idle`, empty `last_error`.

The next documentation-only commit is intentionally used as the newer signed
staging candidate for the loaded-deck pull OTA retest.

The first retest on `M2.2-10-g7e2cc69` confirmed the body retry but exposed the
same idle condition one layer earlier. The origin recorded a complete HTTP 200
bundle request and advertised the expected `Content-Length`; the ESP-IDF client
timed out before receiving response-header bytes. Because the code inspected
the unset status code after the negative `esp_http_client_fetch_headers()`
result, the UI incorrectly reported `bundle not on the server`.

Commit `626c2d9` applies the same three-window bound to response-header fetches
and refuses to inspect the HTTP status until headers have parsed successfully.
A real non-200 status still maps to `ESP_ERR_NOT_FOUND`; exhausted header idle
windows now report `ESP_ERR_TIMEOUT` with the `read headers` stage.

- Full P4 host suite: PASS after the response-header fix.
- ESP-IDF `v6.0.2` build: PASS after the response-header fix.
- Exact signed remediation image: `M2.2-12-g626c2d9`, 2.506.464 B,
  SHA-256 `9b75c848d2fdc877e6a00c6b15c8ff218227d678faa122372a1b940964d6262b`.
- Signed bundle: 2.506.652 B, SHA-256
  `0a153668897925747632e62f71f9a01c1099522da4db7aca54493264193f66d6`.
- Signed local push OTA: PASS, `M2.2-10-g7e2cc69` on `ota_1` to
  `M2.2-12-g626c2d9` on `ota_0`; firmware state `idle`, empty `last_error`.

## Production-channel pull OTA

The final candidate was built and signed as `M2.2-13-ge0f9add`:

- application: 2.506.464 B, SHA-256
  `eb3439702c3c590c1ff8a24df3fb19f47cbbd8671b41d2e7c3b91b3edc6d2624`;
- signed bundle: 2.506.652 B, SHA-256
  `b5b48161fa5cc346b2b55a2e7f4296672b59aa8211bb228bcce859b0825db959`;
- ECDSA P-256/SHA-256 key `rel-001`; package verification PASS.

The versioned bundle was uploaded over explicit FTPS and independently fetched
from
`https://ota.pajoniiir.eu/M2.2-13-ge0f9add/main-deck-p4.ddjota`. The public
download had the exact expected size and SHA-256 before `latest.json` was
changed. The channel was then temporarily set to the candidate, and the
installed `M2.2-12-g626c2d9` image reported
`update available: M2.2-13-ge0f9add` through the canonical HTTPS origin.

The first install attempt ended in a task-watchdog reset while the old
`M2.2-12-g626c2d9` image was still running. The retained crash summary names
`controller_usb`, with IDLE0 as the task-watchdog victim; there is no evidence
that the candidate slot booted during this attempt. The device recovered on
`ota_0` with both decks idle and zero PCM/UAC counters. This is an intermittent
open finding rather than a successful OTA attempt.

A controlled retry from that clean idle state completed the signed pull OTA.
The device rebooted into `M2.2-13-ge0f9add` on `ota_1`; `/api/firmware` reported
`idle` and an empty `last_error`. Post-install checks confirmed:

- DDJ-FLX4 `2B73:0045` present with active `pioneer_ddj_flx4` profile;
- USB storage mounted and 324 library tracks available;
- two real tracks loaded, then both decks played continuously for 15 seconds;
- D1/D2 advanced 15.110/15.111 seconds and both stopped cleanly;
- zero PCM underruns, output-late events, UAC dropped blocks, overflow frames,
  packet failures and lost frames;
- zero USB daemon errors and zero service-log drops.

A following 180-second dual-deck digital soak used a 728-second FLAC on D1 and
a 493-second MP3 on D2. Eighteen ten-second samples kept the exact candidate
version running with both decks continuously playing. D1/D2 advanced
184.367/184.366 seconds; PCM, output-late, UAC, USB daemon and service-log error
counters remained zero, and automated cleanup stopped both decks.

After the test, public `latest.json` was restored to immutable production
release `M2.2`, 2.493.660 B, SHA-256
`5552d32527e55d7393fe89a49bdf1b753209af8d2a788f9e8d83fbda84ba0676`.
A channel check from the installed candidate then returned
`older release ignored; use signed local upload to roll back`, confirming the
newer-only policy and that no downgrade was attempted. The device keeps the
canonical `https://ota.pajoniiir.eu` configuration.

This automated smoke proves channel discovery, signed download/install,
boot-slot selection and digital playback health for the exact candidate. It
does not provide acoustic/listening acceptance. The intermittent
`controller_usb` watchdog from the first install attempt must remain visible in
release assessment until reproduced and resolved or closed by a defined soak
limit.

## Ten-cycle software-reboot matrix

The installed `M2.2-13-ge0f9add` candidate then completed ten consecutive
guarded `/api/validation/reboot` cycles with USB0 media and the DDJ-FLX4
continuously attached. The service-log boot sequence advanced without a gap
from boot 530 through boots 531--540, and every new header reported reset reason
`SW`, the same `ota_1` slot and the same firmware version.

Every cycle restored both powered USB roots, the active
`pioneer_ddj_flx4` profile and the complete 324-track library. OTA state stayed
`idle` with an empty `last_error`; topology, recovery, daemon, runtime queue,
service-log, PCM, output-late and session UAC fault counters remained zero. The
current-boot audio watchdog flag stayed clear.

The retained pre-matrix crash dump remained unchanged across all ten cycles:
14.656 bytes, task `controller_usb`, PC `0x4FF0DAC8`. It is therefore historical
evidence from the failed first pull attempt rather than a new reboot-matrix
panic. The ten-cycle reboot criterion is PASS. The cause of the earlier
pull-install watchdog is still not proven, so this result narrows but does not
erase that separate release finding.

## Wi-Fi OFF/ON ESP-Hosted lifecycle panic

An operator Settings OFF -> ON cycle on `M2.2-13-ge0f9add` produced a new,
fully diagnosed boot 541 panic. The service journal recorded `WIFI_STOPPED` at
1,465,143 ms and a new `WIFI_ENABLE_REQUESTED` at 1,474,690 ms, followed by a
reset before `WIFI_STARTED`. The retained 8,192-byte coredump identifies task
`wifi_link` and panic reason
`assert failed: bus_init_internal sdio_drv.c:1530 (sdio_handle)`.

Commit `a1cc05c` makes ESP-Hosted transport ownership boot-scoped. Wi-Fi OFF
still stops and deinitializes the remote `esp_wifi` interface, AP netif, HTTP
and DNS services, but retains the SDIO control transport needed by a later ON
request. The P4 host suite locks this invariant by rejecting any
`esp_hosted_deinit();` call in `wifi_link.c`.

The first signed candidate exposed a separate release-version issue. Publishing
the prerelease tag `M2.2-13-ge0f9add` causes later `git describe` output to take
the chained form `M2.2-13-ge0f9add-<distance>-g<hash>`, which the newer-only
parser previously rejected. Commit `cde901d` parses every distance/hash suffix,
sums the distances from the milestone and retains fail-closed behavior for
malformed, overflowing or equal-distance divergent histories.

Pre-merge review also identified the fixed 31-byte payload limit of
`esp_app_desc_t.version`. The P4 CMake entrypoint now derives `PROJECT_VER`
before ESP-IDF configuration, excludes distribution tags containing a derived
`-g<hash>` suffix from ancestry selection, and fails configuration when the
resolved UTF-8 value exceeds 31 bytes. This keeps existing chained versions
orderable for installed images while preventing another chain in new builds.

The resulting signed candidate `M2.2-13-ge0f9add-12-gcde901d` was built with
ESP-IDF v6.0.2 and installed by signed local push OTA on `ota_1`, boot 543:

- application: 2,505,168 B, SHA-256
  `639998a7840d2c30d341d920c15dde6f4d38cde22225dda38cd9f6c0271f97b2`;
- signed bundle: 2,505,356 B, SHA-256
  `d0dbf037cb32ab4d94b94cae540d5dfe3b25d711fe0aaf44e2c46409bdb3a1e9`;
- ECDSA P-256/SHA-256 key `rel-001`; independent verification PASS;
- full P4 host suite PASS, including chained-version and persistent-transport
  regressions;
- ten consecutive canonical production-channel checks PASS on boot 543; every
  check returned `older release ignored; use signed local upload to roll back`;
- FLX4 `2B73:0045`, active `pioneer_ddj_flx4`, both USB roots and the mounted
  324-track library remained available with no reboot;
- final 15-second dual-deck smoke advanced FLAC/MP3 positions by 15,238/15,058
  ms, stopped both decks automatically, and kept PCM underrun, output-late,
  UAC data-loss/overflow/packet-failure, USB daemon, service-log drop and
  current TWDT counters at zero.

The old boot-541 coredump remains intentionally retained and unchanged as fault
evidence. A physical Settings OFF -> ON repetition on this exact candidate is
still required because turning Wi-Fi OFF removes the only remote control path;
it cannot be completed through the web API alone. The public M2.2 channel and
immutable release assets were not changed.
