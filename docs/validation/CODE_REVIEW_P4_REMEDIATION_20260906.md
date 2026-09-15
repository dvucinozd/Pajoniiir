# P4 branch review remediation - 2026-09-06

## Scope and disposition

Implements R1-R10 from [the branch review](CODE_REVIEW_P4_BRANCH_20260906.md)
on `feat/p4-dual-usb-host`, based on
`af597d8813f8a6a7fc20898bb25b5943230edab1`. Results below describe the
pre-publication verification of this remediation patch.
The pre-existing documentation migration, staged paths and untracked replacement
documents were preserved. No device was flashed and no release was published.

These are software fixes with host/build evidence. Physical P4 scheduling,
storage failure, USB hot-plug and long-soak acceptance remain open.

## Changes

| Finding | Implemented behavior | Regression evidence |
| --- | --- | --- |
| R1 | Controller runtime uses a FreeRTOS mutex with priority inheritance instead of a task-context spin loop. Init fails if mutex allocation fails. Host builds use a pthread mutex. | Firmware mutex API path compiled against pthread boundary stubs; allocation failure, 10,000 concurrent MIDI messages, dispatch and callback reentry pass. Actual FreeRTOS priority inheritance timing still requires P4 testing. |
| R2 | Both startup-abort branches retain worker count, handles, context and preload while any worker remains unjoined. A later STOP completes teardown. | Extracted production abort branches: partial exit timeout preserves ownership; delayed final exit permits safe cleanup. Both complete-join paths also pass. |
| R3 | Each consumed task-exit acknowledgment decrements the outstanding count immediately, so repeated STOP attempts retain partial progress. | Production STOP with one of two workers exiting, another timeout, final worker exit and repeated successful STOP. |
| R4 | Output EOF completion and scratch-abort seek use a zero-wait metadata mutex acquisition and retry on subsequent output turns. The scratch mailbox stays pending while blocked. | Extracted production EOF and scratch-abort branches defer while the lock is unavailable, then finish/publish once acquired. Scratch seek callback is checked for existing recursive-lock ownership. |
| R5 | A single atomic admission/producer-ownership word prevents UAC cleanup from resetting ring/resampler state during a write. Cleanup polls without blocking the USB task. | Deterministic STOP immediately before ring publication at 44.1 and 48 kHz; cleanup defers, rejects another writer, then cleans and reconnects successfully. |
| R6 | Completed USB transfers inspect individual packet statuses and lengths. Packet failures and lost frames reach stream/host diagnostics, `/api/status`, service logs and the active playback data-loss flag (`1 << 5`). Isolated packet loss does not reset USB. | Mixed completed/skipped/error/short packets report three failures and 90 lost frames; a healthy completion adds no loss; terminal transfer error retains fault/cleanup behavior. Health tests cover baseline, active loss, latching and idle reset. |
| R7 | Connection delivery uses a durable runtime mailbox and acknowledges only accepted generations. A pending disconnect is delivered before a newer reconnect, preserving platter release and LED refresh. | Saturated 64-event queue, repeated callback rejection, rapid disconnect/replug and a new disconnect during callback delivery all converge correctly. Existing deck-core duplicate-state behavior remains unchanged. |
| R8 | Hardware output rate always selects 44.1 or 48 kHz for nonzero source rates. Low-rate tracks are upsampled through the existing deck resampler before reaching UAC. | Selector covers 8, 22.05, 32, 44.1, 48 and 96 kHz plus zero; production rate selection builds. Actual 32 kHz cue playback remains a hardware smoke item. |
| R9 | Restored `tests/controller_usb_audio/run_tests.ps1` with production stream, ring, resampler, packetizer and descriptor code against boundary stubs. Main P4 runner now includes lifecycle and controller runtime/USB/LED suites. | Main runner executes the previously omitted gates, including nested UAC execution; standalone UAC runner also passes. |
| R10 | LED fixture expectations match the tested dynamic, built-in, unsupported and send-failure paths. | LED suite passes 36 checks / four sends; no production LED behavior changed. |

The separate integration-harness build also exposed a pre-existing link failure:
`usb_host_lib_power_off_root_port_if_idle_by_index` was referenced by
`usb_host_manager` but the harness omitted the production USB CMake transforms.
Its CMake entry point now includes the same FIFO, indexed idle recovery and MSC
teardown transforms. The dedicated USB CI path filter includes changes to those
shared CMake files.

## Verification

- `tests/run_p4_host_tests.ps1`: PASS on PowerShell 7, exit 0,
  **81 run steps**, including new lifecycle, mutex/concurrency and UAC tests.
  No signing-test skip.
- Windows PowerShell 5.1 full runner: PASS, exit 0, all 81 run steps.
- `tests/ui_simulator/run_ui_simulator_e2e.ps1`: PASS, exit 0, all seven
  screenshot hashes match existing baselines. No baseline was updated.
- `tests/audio_keylock_soak/run_audio_keylock_soak.ps1`: PASS, exit 0,
  300 seconds virtual dual-deck audio, zero drift, zero clicks, zero clipping.
  Frequency errors 0.156% / 0.119%; mixed peak 18752.
- P4 production build: PASS, exit 0, ESP-IDF v6.0.2, isolated
  `build_overall_review_20260906` / SDKCONFIG, target `esp32p4`.
  `firmware/main-deck-p4/dependencies.lock` unchanged.
- Shared-component `p4-only-software-harness` build: PASS, exit 0, ESP-IDF
  v6.0.2, isolated `build_review_remediation_20260906` / SDKCONFIG,
  target `esp32p4`, binary 277,024 bytes (`0x43a20`).
- `git diff --check` and `git diff --cached --check`: PASS.

Production binary: **2,453,024 bytes**, below the 3,670,016-byte budget by
1,216,992 bytes. SHA-256:
`2a4b7abfec467aada617e2d23c61866238dd55577ebd16bd04c1a2a5b774d8cf`.
This identifies the local test image, not a signed/published release.

Local logs and binary-budget JSON are in the ignored
`tmp/overall-review-20260906/remediation-*` files. Host boundary tests do not
measure real-time deadlines or establish electrical/USB acceptance. Required
device follow-up includes slow-media STOP/load retry, EOF/scratch seek during
other-deck decode, controller reconnect under load, 32 kHz source cue output,
and the existing strict dual-USB playback soak and VBUS qualification.
