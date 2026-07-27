#!/usr/bin/env python3
"""Apply unapplied audit phases serially and idempotently."""

from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

PHASES = [
    ("firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c",
     "X-Log-Seq",
     "tools/apply_release_audit_phase3_s3_debug_ap.py"),
    ("firmware/control-board-s3/components/p4_audio_link/p4_audio_link.c",
     "Drop newest",
     "tools/apply_release_audit_phase4_s3_audio.py"),
    ("firmware/main-deck-p4/components/ui/ui_deck_anlz_store.c",
     "ui_deck_anlz_store_clone",
     "tools/apply_release_audit_phase5_anlz_ownership.py"),
    ("firmware/main-deck-p4/components/audio_engine/audio_engine.c",
     "s_master_tempo_command_epoch",
     "tools/apply_release_audit_phase6_audio_lifecycle.py"),
    ("firmware/main-deck-p4/components/control_link/control_link_uart.c",
     "store_pending_continuous",
     "tools/apply_release_audit_phase7_control_queue.py"),
]

for source_rel, marker, script_rel in PHASES:
    source = (ROOT / source_rel).read_text(encoding="utf-8")
    if marker in source:
        print(f"skip {script_rel}: marker already present")
        continue
    print(f"apply {script_rel}")
    subprocess.run(["python3", str(ROOT / script_rel)], cwd=ROOT, check=True)

print("All pending audit phases applied.")
