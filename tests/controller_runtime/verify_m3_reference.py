"""Check that test references retain the frozen M3 source logic verbatim."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).with_name("legacy_m3")
manifest = json.loads((root / "provenance.json").read_text())
assert manifest["source_sha"] == "e95417c4e2fea007d2c1dcb693790914692c62ba"
for item in manifest["files"]:
    text = (root / item["test_reference"]).read_text()
    original = text.split("*/\n", 1)[1].replace("legacy_m3_flx4_map.h", "p4_flx4_map.h")
    original = original.replace("legacy_m3_flx4_", "flx4_")
    assert hashlib.sha256(original.encode()).hexdigest() == item["sha256"], item["source"]
print("PASS frozen M3 mapper/LED reference provenance")
