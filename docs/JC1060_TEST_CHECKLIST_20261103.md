# JC1060 / DDJ-FLX4 candidate: physical checklist

Status: **NOT RUN**. Use the
[signed ordinary and WSOLA2 pair](JC1060_COMPLETION_20261103.md#2026-11-27-merge-and-signed-ab-packages)
from `1f364512`. Record each flashed image's source, bundle SHA-256, ELF SHA-256
and configuration. Both packages report the same version. SD cold power is a
separate configuration with its own package and evidence.

## 0. Identify and preserve recovery

- Begin when the operator confirms COM3 is free.
- Record exact board/panel/silicon, flash size, PSRAM, controller VID/PID,
  current firmware/version/ELF/slot, partition table and security state.
- Preserve and verify independent full-flash backups plus relevant NVS,
  partition, boot/selector and crash evidence before the first write. Use the
  actual reported flash size; do not infer offsets from this candidate.
- Establish the correct serial port and its DTR/RTS reset behavior. Open the
  intended capture once before a run; opening serial mid-run may reset a board.
- Match actual board/layout/trust key before choosing wired install or signed
  local OTA. Do not erase the chip or blindly rewrite partitions/otadata.
- Ordinary package first. Keep its BIN/ELF/debug files and signed bundle for
  recovery. No eFuse operation or C6 firmware update is part of this checklist.

## 1. Ordinary boot and navigation

- Controller on board USB0/root0, storage USB1/root1; record actual cabling.
- Confirm 1024x600 geometry, touch corners, backlight and screensaver wake.
- Ethernet DHCP and HTTP ready; source/ELF/board/project match the manifest,
  source_dirty=false and accepted image state. Record actual library count.
- Browse/load both decks; LEDs and PFL behavior. Test first-row folder/playlist
  touch, nested open/Back and export order. Track tap must not load a deck.
- LOAD LOCK ON blocks playing-deck replacement via touch/controller/web. OFF
  admits the intended replacement. Stop and verify the setting across reboot;
  restore ON before the playback tests.

## 2. Memory banks (stopped decks)

- Use D1 and D2 and two different media identities. Establish a CUE point,
  MEMORY -> STORE, PREV/NEXT and DELETE. PREV/NEXT must never autoplay.
- Save/recall an active loop; boundaries and the current CUE agree. Duplicate
  STORE replaces the same start; bank capacity is 16. Unknown/failed operations
  must not be reported as success. Playing-deck recall is refused.
- Reload/reboot: local edits persist. Another track/export must not inherit
  them. Deleting all cues keeps an empty override. A short HOLD SOURCE click
  does nothing; holding restores source memory cues without changing Hot Cues.
- DDJ-FLX4 uses touch MEMORY controls; the new DDJ-400 MIDI memory mapping is
  software-tested only until that separate physical controller is tested.

## 3. Short audio and USB MAIN recovery

- Start headphones LEVEL low. Load mixed-rate/format sources (record each),
  both decks, opposite pitch, MT/loop and waveform zoom 8.
- Exercise jog/Browse, MAIN/PFL D1/D2, MIX/LEVEL, CUE-to-PLAY, loop resize and
  loop exit. Record listening/display observations separately from counters.
- USB controller removal must pause loaded decks at the last audible heads.
  Reconnect must not autoplay. Explicit PLAY resumes each deck without LOAD;
  titles, track identity, cue state and positions stay coherent. Repeat with
  each deck alone and both loaded. Preserve the first-fault evidence if failed.
- Measure strict before/after counters. Missing telemetry is unavailable, not
  zero. Report output-late count/max/clusters separately; do not weaken gates.

### Master Tempo A/B for issue #50

- Complete the ordinary image smoke above first. Use the same DDJ-FLX4,
  tracks, excerpts, levels and tempo offsets on both images. Include 44.1 kHz
  and 48 kHz material and listen through the MT toggle and a tempo change.
- Run each image with MT enabled on both decks. Capture output-late count,
  maximum and clusters before and after, plus listening notes on audible
  distortion and dropouts. Bind each result to its ELF and bundle hashes.
- Record the comparison against
  [issue #50](https://github.com/dvucinozd/Pajoniiir/issues/50). Return to the
  ordinary image for the remaining checklist unless the operator chooses a
  different candidate after reviewing the results.

## 4. Wi-Fi alongside Ethernet

- Fresh/default JC1060 Wi-Fi is OFF. Enable its separate Settings switch:
  WPA2 APSTA `Pajoniiir-JC1060`, HTTP/AP address and mDNS.
- Toggle Wi-Fi three times. Ethernet HTTP must remain usable throughout and
  Ethernet DJ LINK must retain its independent setting. Test saved OFF/ON on
  reboot, AP -> configured STA -> AP round trip and intended STA download.
- Repeat short active playback with API polling and observe both audio and UI.
  Read C6/Hosted versions; do not silently update the C6 if they differ.
- Link remains Ethernet-only. With a named real peer or separately identified
  simulator, test browse, cancel during resolve/download, peer loss/return,
  artwork, source/media swap and absence of stale LOAD results. Audio already
  loaded must continue. Real CDJ phase/drift/handoff remains a separate test.

## 5. Separate SD cold-power candidate

- Stop decks; identify card, slot-0 pins and LDO rail ownership. Verify the
  separate signed SD package and its ELF/configuration before installation.
  Prepare a paired SD variant from the source selected for this test. The
  A/B packages in the current kit both have SD cold power disabled.
- Same card: at least 10 warm resets and 10 true cold boots; retain every mount
  result and read/write verification. Test no card and mount failure behavior.
- Verify USB and requested Hosted/Wi-Fi continuity; no global SDMMC reset.
- Restore the ordinary candidate after the experiment unless retaining the SD
  image is explicitly selected and documented after hardware acceptance.

## 6. Pending OTA readiness and rollback

- Start only after wired recovery and an accepted known ordinary slot are
  confirmed. Stop both decks and preserve current slot/boot/ELF snapshots.
- First confirm successful signed local OTA and post-boot exact identity.
- A controlled missing required Ethernet/DHCP readiness on a pending image
  must reject it at the existing 60-second deadline and return to the previous
  accepted slot. Saved enabled Wi-Fi adds its own readiness condition; account
  for that setting when selecting the failure case.
- Capture boot/slot transition without resetting away evidence, reconnect the
  management path, confirm the restored ordinary image, GUI, USB and playback.
  Do not fabricate failure by corrupting partitions or changing eFuses.

## 7. Exact-image long run

Only after the bounded cases pass, choose the final ordinary image and record
its exact source/ELF/configuration. Run the documented uninterrupted 180-minute
dual-deck workload with operator listening/display acceptance. An explicit
waiver is NOT RUN, not PASS. Existing M3/JC4880 waivers do not apply to JC1060.

Use the existing read-only monitor with actual endpoint and library count:

```powershell
./tools/monitor_p4_reliability.ps1 -ExpectedBoard jc1060 `
  -BaseUrl <actual-http-endpoint> -ExpectedSourceSha <sealed-source-sha> `
  -ExpectedElfSha256 <sealed-elf-sha256> -ExpectedLibraryTracks <actual-count> `
  -Mode Observe -DurationSeconds 60 -OutputDirectory <new-evidence-directory>
```

Observe alone does not prove a dual-deck workload; retain raw snapshots and an
offline workload/counter audit plus operator observations. Avoid unrelated
resource walks/large log downloads during the first audio window. Any later
monitoring stress is a separately named test.
