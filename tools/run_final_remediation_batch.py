#!/usr/bin/env python3
"""Adapt the final batch to run from the permanent workflow slot, then execute it."""

from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
batch = root / "tools/apply_final_remediation_batch.py"
text = batch.read_text(encoding="utf-8")

start_marker = "# 4. Restore the single permanent CI after this batch is validated."
end_marker = "# 5. Remove every one-shot transformation workflow/script."
start = text.index(start_marker)
end = text.index(end_marker)
replacement = '''# 4. Restore the single permanent CI after this batch is validated.
# The running Actions job already loaded its temporary definition, so replacing
# the same file here is safe; the validated commit starts one normal CI run.
import shutil
ci_rel = ".github/workflows/esp-idf-6-migration.yml"
ci_template = ROOT / ".github/esp-idf-6-migration.permanent.yml"
shutil.copyfile(ci_template, ROOT / ci_rel)

'''
text = text[:start] + replacement + text[end:]

needle = '    ".github/workflows/apply-final-remediation-batch.yml",\n'
if needle not in text:
    raise RuntimeError("transient workflow anchor missing")
text = text.replace(
    needle,
    needle
    + '    ".github/esp-idf-6-migration.permanent.yml",\n'
    + '    "tools/run_final_remediation_batch.py",\n',
    1,
)
batch.write_text(text, encoding="utf-8")
subprocess.run(["python3", str(batch)], cwd=root, check=True)
