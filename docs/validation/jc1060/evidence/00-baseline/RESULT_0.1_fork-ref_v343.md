# JC1060 result: 00-baseline known-good image (fork-ref v343)

| Field | Value |
| --- | --- |
| Date / operator | 2026-10-11 / kay |
| Plan scenarios | 0.1 (0.2-0.4 apply to the shared-core image, separate result file) |
| Board / panel revision | JC1060P470C_I_W_Y / not captured (A1) |
| `/api/status` board, project | not reported by this image (no identity keys) |
| Version string | `pajoniiir_deck 343`, built 2026-10-05 19:44:01, ESP-IDF v6.0.2 |
| Source SHA | unknown: fork-ref HEAD `92374f35` (titled v336-v340); v341-v343 not verifiably committed |
| ELF SHA-256 | not available; app `.bin` SHA-256 `8e875aac3e337dcc0bb273b136fd754b30f0dcaae05af8de8358394c7f7654e9` (see `RECOVERY_IMAGE.md`) |
| Flash method | already on unit (not flashed this session) |
| Peripherals | SD02G 1910 MB SDSC (mounted on attempt 2/5), Ethernet DHCP 192.168.100.83; no DDJ-400 / USB stick recorded |
| Build flags | regular (fork-ref image) |

## Results

| Scenario | Result (PASS / FAIL / NOT RUN / WAIVED) | Evidence file | Notes |
| --- | --- | --- | --- |
| 0.1 | PASS | `boot_fork-ref_v343_poweron.log`, `api_status_fork-ref_v343.json` | POWERON boot after manual RST, slot ota_0 `state=valid`, UI reached (operator confirmed), no panic/WDT/`alloc FAILED`/underrun in log. Image identity limited to version + app SHA (no `/api/status` identity) |

## Anomalies

- SD mount attempt 1/5 failed (`sdmmc_card_init 0x107`, then `sd_host_del_sdmmc_controller: host controller slot registered`); attempt 2 mounted. Same error family as E9.
- Internal heap low after USB bring-up: `internal free=40619 largest=15872` before the web server start.
- `p4_usb.host`: `recovery_requests` 14 / `recovery_successes` 14 / failures 0 in one boot.
- DJ Link is started at boot on this image (Link is OFF by default on the shared core, F3).
- `/api/resources` answers 302 to `pajoniiir.local/index.html` on this image; `api_resources_fork-ref_v343.json` is an empty redirect body, not a capture.

## Decision

Continue to 0.2, but only after a full flash backup: the shared-core partition table (factory 0x20000/0x400000, ota_0 0x420000, ota_1 0x820000, coredump 0xc20000) differs from this image (ota_0 0x20000/0x300000, ota_1 0x320000, coredump 0x620000), so rollback needs the old bootloader and partition table, whose on-flash versions are not known.
