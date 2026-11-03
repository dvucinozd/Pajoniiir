# JC1060 completion: local software evidence

Source `8bc3b53aff98c8b624696c64addbb8c1b5a4c577`, clean, ESP-IDF 6.0.2.
Scope and decisions: [candidate record](../../../JC1060_COMPLETION_20261103.md).
Physical sequence: [checklist](../../../JC1060_TEST_CHECKLIST_20261103.md).

- `matrix.json`: four build identities/bytes/hashes/configurations, unchanged
  locks, host step counts, nine UI presentations/145 captures and package hashes.
- `offline-verification.json`: signed 51-file delivery inventory, source recovery
  and modified-bundle/inventory rejection results. No private material included.
- `jc1060-final-host.txt.gz`: complete `run_p4_host_tests.ps1 -Qualification` output,
  exit 0, 144 functional build steps / 206 run steps, no optional SKIP.
- `jc1060-final-build.txt.gz`, `jc1060-final-main-deck-p4.txt.gz`,
  `jc1060-final-main-deck-m3.txt.gz`, `jc1060-final-sd-build.txt.gz`: IDF build logs.
  Ordinary builds reuse existing directories; SD is a fresh-config full build.
- `jc1060-ui-final-*.txt.gz`: nine successful final simulator logs, unchanged or
  visually reviewed capture baselines. `completion_memory`/`completion_settings`
  exercise actual JC1060 touch controls; these are software mocks at device
  boundaries, not physical PPA/DSI/touch/audio acceptance.
- `jc1060-package-ordinary.txt.gz`, `jc1060-package-sd.txt.gz`: separate local signed
  packaging/verification output, not public delivery or installation.
- Four PNGs preserve reviewed wide memory/Settings and compact/M3 UI examples.

Original log bytes are retained as losslessly compressed generated text;
diagnostic strings and PowerShell formatting are not edited. Matrix SHA-256
values bind the decompressed source logs, verified after compression.
The directory's attributes preserve generated JSON bytes and LF documentation;
`SHA256SUMS.json` authenticates the retained evidence through Git history.
No GitHub Actions run was dispatched or intermediate source pushed. COM3 belongs
to the operator's JC1060 and is held by another agent. No device access, serial
open, API request, reset, backup or write occurred. All physical gates are NOT RUN.
