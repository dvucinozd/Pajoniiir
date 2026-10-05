# Package K: network sync and tempo master

Date: 2026-10-05. Software closure on `codex/fork-improvements`, based on
`298ee029253183015454535451de218de2781e68` (J, all eight hosted jobs PASS).
The final package commit and exact hosted run are reported at delivery.
Production M2.4 and the last-known installed JC4880 `M2.4-61-gc4912d5b` are
unchanged. No OTA, COM access, signing or public-channel change was performed.

## Provenance and behavior

The pure `deck_net_sync` model and its simulation originate from
`kayrozen/Pajoniiir@428b97dd4a175f03d3a172c8db9c4d5ed94195fb` under MIT,
Copyright (c) 2024 The Pajoniiir Contributors (root LICENSE). Existing djlink
codec licensing remains in `firmware/common/djlink/LICENSE`. This is a narrow
port, not a donor firmware merge. APTA is excluded.

- The deck task evaluates the accepted audio timeline every 40 ms. Clock
  snapshots carry source/master epochs, player, monotonic anchor, period and
  beat 1-4. Unknown bars cannot report LOCKED. Stale limit is 2.5 beats; tempo
  range is +/-20%, phase trim +/-1%, deadband 0.004 beats. LOCKED requires raw
  bar error and filtered beat error below 0.05 beats with a fresh clock.
- Explicit SYNC engage or PLAY can request one alignment. Periodic hard resync
  is removed. Paused/CUE/jog resume, source changes and scratch hold do not arm
  a seek. Scratch cancels pending alignment and does not change pitch. Large
  persistent bar errors stay ALIGNING until an explicit alignment. Master loss
  retains the last applied tempo in WAIT and cannot select an arbitrary peer.
- Deck mutations use a nonblocking lifecycle try-lock and recheck the accepted
  audio session, loaded state and scratch/hold before issuing existing seek or
  atomic pitch commands. Replaced/pending loads cannot accept old clock output.
  The load worker, audio output and LVGL ownership boundaries are unchanged.
- One worker owns the heap-free coordinator and Ethernet socket sends. Its
  bounded mailbox carries owned values only; data older than 120 ms is rejected.
  JC4880 installs no provider, retaining local sync. Link remains default OFF.
- Initial selection requires exactly one advertised master. Local takeover
  uses the codec's request/reply with source IP/player/epoch and claim checks.
  Outgoing handoff retains authority until the requested peer confirms MASTER,
  with a three-second timeout. Remote handoff requires announcement by the
  selected authority and confirmation by the same target/source epoch. Duplicate
  requests cannot extend their deadline. Unrelated peer arrivals/expiry cannot
  revoke the local claim; changes to own IP/MAC/numbers or observer state do.
- Both claimed decks emit bounded 200 ms status updates to known peers and
  beat updates through the Ethernet-bound beat socket. No catch-up burst or
  local browsable library is advertised. Packet progress is derived from owned
  accepted-timeline snapshots; stale or held local clocks cannot drive beats.
- Sink latency is keyed to sink and sample rate (44.1/48 kHz). The engineering
  setter accepts measured values up to 500 ms, stores RAM only and defaults to
  UNMEASURED. Wrong sink/rate applies zero compensation. No physical measurement
  is supplied by this package; calibration must be reapplied after reboot.
- Previous-design Overview shows WAIT/ALIGNING/LOCKED and reference player;
  existing loading/errors retain priority. Settings reports calibration state.
  Web Remote adds optional network state/player/error/calibration fields while
  retaining previous fields. Touch and controller use the same deck events.

## Automated verification

Local ESP-IDF is v6.0.2. Both ordinary targets build with unchanged dependency
locks and pass `tools/check_board_build.py` (isolated BSP, LVGL 9.5.0, matching
project identity and the unchanged 0x380000 application budget):

| Target | Application bytes | Slot bytes |
| --- | ---: | ---: |
| main-deck-p4 | 2,565,808 | 3,670,016 |
| main-deck-jc1060 | 2,624,640 | 3,670,016 |

The following evidence belongs to software qualification only:

- Full P4 host runner, including v2/v3/v4 profiles, identity/cache/lifecycle,
  signed OTA and existing transport regressions. Focused deck/scratch suites
  exercise actual shared semantic SYNC/PLAY/master and stale-session behavior.
- Pure clock simulation covers both signs of 400 ppm drift over two minutes,
  3 ms jitter, loss, clock wrap, unknown bars, out-of-range tempo, epoch changes,
  scratch/implicit resume, master jump without automatic seek and latency scope.
- Coordinator tests cover ambiguous/sticky masters, reordered/lost packets,
  swapped sources, spoofed/duplicate/expired handoffs, local and remote handoff,
  unrelated peer inventory changes, own claim invalidation and outgoing packets.
- Linux ASan/UBSan runs the network codec, actual localhost UDP/TCP mocks,
  cache/analysis and new sync model. UDP checks all three bound source ports.
  A compiled extraction of the production audio mutation verifies busy-lock,
  session/hold/range checks and mutation admission (not real RTOS scheduling).
- All eight simulator presentations: legacy, product compact/wide, native
  compact/wide, runtime compact/wide and Link wide. Three new status screenshots
  in each ordinary layout were visually reviewed before adding hashes; existing
  screenshot hashes remain unchanged. Generated JSON whitespace is preserved.
- Separate 300-second virtual dual-deck Master Tempo soak: zero consumed-frame
  drift, clicks or clipping. This is not a physical 180-minute soak.
- Documentation integrity, dependency-lock checks and `git diff --check`.

Reproduction:

```powershell
$env:Path += ';C:\msys64\ucrt64\bin'
./tests/run_p4_host_tests.ps1
./tests/audio_keylock_soak/run_audio_keylock_soak.ps1
./tests/ui_simulator/run_ui_simulator_e2e.ps1 -Presentation product-wide -KeepArtifacts
wsl -d Ubuntu -- bash -lc 'cd /mnt/c/Users/Daniel/.codex/worktrees/fork-improvements/DDJ-FFL4 && bash tests/djlink/run_linux_sanitizers.sh'
```

Ignored evidence: `.cache/k_host_final.log`, `.cache/k_sanitizers_final.log`,
`.cache/k_soak_final.log`, `.cache/k_ui_final_*.log`, and target `build_k_final.log`.
Builds run sequentially because the Windows Component Manager shares its Git
cache. An initial parallel build encountered that cache's index lock; sequential
builds passed without changing dependencies or removing tests.

## Physical gates and rollback

| Gate | Result |
| --- | --- |
| Model/runtime admission, simulated drift/loss and handoff | PASS, automated |
| Both ordinary firmware builds and unchanged locks/budget | PASS |
| Eight real-presentation simulator paths | PASS |
| Real JC1060/CDJ and rekordbox beat/status/handoff interoperability | NOT RUN |
| MAIN output phase, drift, sink latency and listening per sink/rate | NOT RUN |
| Scratch/PLAY/SYNC under real USB/audio/network load | NOT RUN |
| JC1060 task timing, active memory and stack reserves | NOT RUN |
| JC4880 audio/reconnect and final 180-minute exact-image soak | NOT RUN; operator deferred |

LOCKED confirms only the clock model's thresholds. It cannot establish audible
alignment or peer interoperability. Hardware acceptance must record board,
controller, peer version, sink/rate, measured latency and exact firmware SHA.
No unmeasured calibration may be promoted to a measured result.

Software rollback is the J parent revision; there is no new persistent format
or device installation to migrate. Link OFF disables this follower; ordinary
JC4880 has no network transport. Next is package L: compatibility and reproducible
candidates, with physical acceptance and public release kept separate.
