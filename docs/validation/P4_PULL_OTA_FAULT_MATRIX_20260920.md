# P4 reduced pull-OTA fault matrix — 2026-09-20

Status: **SDIO panic remediation and public HTTPS channel check PASS on exact
image; M2 pull installation pending**.

## Candidate and channel

- Installed firmware: `RC2-153-g66b5fee-dirty`
- Source-equivalent baseline: `b9139e1bdab79e7d0aec279383e0b8f859d3eea3`
- Initial slot: `ota_1`
- Initial service-log boot epoch: `472`
- Canonical channel root: `https://ota.pajoniiir.eu`
- USB0, FLX4 profile, MIDI IN/OUT and UAC were healthy; both decks were stopped.

The device NVS URL was changed from the retired channel to the canonical root
without changing the stored service-network SSID or passphrase. Upload account
credentials remain out of Git, firmware, release artifacts and this record.

## Network round trip

The guarded connectivity probe completed AP -> STA -> AP in about 13 seconds.
The service network assigned `192.168.0.241`, the probe reported
`round trip complete`, and the Pajoniiir AP returned. This proves the basic
network transition but not the HTTPS channel.

## Reproduced failure

A subsequent read-only `Check for update` was accepted with HTTP 202. During
the longer service-network visit, the device restarted before a channel result
could be read. Persistent evidence shows:

```text
boot 472: WIFI_STOPPED
boot 472: WIFI_ENABLE_REQUESTED
boot 473: reset=PANIC
assert failed: bus_init_internal sdio_drv.c:1530 (sdio_handle)
task: wifi_link
```

Firmware and slot remained `RC2-153-g66b5fee-dirty` / `ota_1`. Boot 473 then
recovered USB0, all 324 Library tracks, the FLX4 profile, MIDI IN/OUT, UAC and
the Pajoniiir AP automatically. This recovery does not turn the reboot into a
PASS.

## Root cause and remediation

Probe and pull-OTA workers already serialize AP -> STA -> AP ownership through
`wifi_transition_lease`. The asynchronous Wi-Fi ON/OFF worker did not take that
lease before running the full `wifi_link_stop()` / `wifi_link_start()` path.
It could therefore deinitialize ESP-Hosted/SDIO while pull OTA still owned the
radio transition, then re-enter `esp_hosted_init()` during teardown.

`WIFI_TRANSITION_OWNER_CONTROL` now serializes full operator ON/OFF lifecycle
changes with probe and OTA transitions. The worker preserves the latest desired
state while waiting, re-samples it after acquiring the lease, and releases the
lease before retry backoff. A host source-contract regression prevents this
ownership boundary from being removed silently.

## Retest protocol

1. Run the full P4 host suite and an ESP-IDF v6.0.2 signed build.
2. Package and install the exact signed image.
3. Repeat the AP -> STA -> AP probe.
4. Repeat `Check for update` against the canonical HTTPS root and require a
   bounded `no update published`, `up to date` or valid offer result without a
   reboot.
5. Confirm unchanged boot identity during each transition plus healthy USB0,
   Library, FLX4 MIDI/UAC and web recovery.

Publication and remote-channel content verification remain separate: from the
Pajoniiir captive AP, DNS intentionally resolves public names locally, so the
public HTTPS files must also be checked from an external network.

## Exact-image retest

The source remediation passed the complete P4 host suite and ESP-IDF v6.0.2
`build_signed`. The application image is 2,459,520 bytes, has SHA-256
`b3d2c10526e26b7c3ef17dcf7a0bb47d0c83a593c3d89ab792e2bdf1aba59a31`
and leaves 1,210,496 bytes inside the product binary budget. Packaging and
ECDSA-P256 verification passed as `RC2-155-ga896c45-dirty`, key ID `rel-001`.

The signed push OTA returned HTTP 200 and moved the device from `ota_1` to
`ota_0`. The installed exact image then produced these results:

- AP -> STA -> AP probe: PASS in about 13 seconds, service address
  `192.168.0.241`, unchanged boot identity `12`;
- pull `Check for update`: bounded FAIL, `could not reach update server`;
- no panic, watchdog, reboot or slot/version change during either transition;
- USB0 mounted, all 324 Library tracks restored, FLX4 profile/MIDI IN/MIDI OUT
  and UAC healthy, with zero USB daemon/recovery or service-log failures.

The original SDIO assertion therefore did not reproduce on the remediated
image. The failure returned by the pull check is external and expected at this
checkpoint: public DNS queried through `1.1.1.1` reports `NXDOMAIN` for
`ota.pajoniiir.eu`. The apex and `www` names resolve, but neither `ota` nor
`ftp` currently has an A or CNAME record.

Windows associated with the restored Pajoniiir AP but twice retained an
APIPA `169.254.x.x` address after the longer transition. A temporary static
`192.168.4.2/24` address allowed the unchanged-boot and recovery evidence to
be read. Client DHCP reacquisition should be checked independently with a
second client before treating it as a firmware AP/DHCP defect.

## Public channel closure

The authoritative DNS record and public resolvers now return
`ota.pajoniiir.eu -> 138.201.18.124`. The HTTPS endpoint presents a valid
Let's Encrypt chain whose SAN covers `ota.pajoniiir.eu`. Secure explicit FTPS
publication placed the signed bundle before `latest.json`; credentials were
kept out of Git, firmware, command output and release artifacts.

External HTTPS verification returned HTTP 200 for both files. The published
bundle was 2,459,708 bytes and its downloaded SHA-256 was
`d6b8f8377fdf3835b60a68f2a37395c91312e9d04052f5bfd07dd57baed728b3`,
matching `latest.json` exactly.

After upstream negative DNS caches expired, the installed device completed a
real AP -> STA -> HTTPS channel read -> AP round trip and reported
`already running this build`. Firmware remained
`RC2-155-ga896c45-dirty` on `ota_0`, boot identity remained 12, and USB0,
FLX4 profile, MIDI IN/OUT and UAC stayed healthy with zero daemon or recovery
failure and no TWDT evidence.

The remaining positive-path test is a genuinely newer public pull install.
That is coupled to the planned `RC2` -> `M2` prefix migration: install a local
signed RC2 bridge that understands the new family, publish the same commit as
the annotated `M2` tag, then require the device to download, verify, activate
and boot that public artifact on the opposite slot.
