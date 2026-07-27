#!/usr/bin/env python3
from pathlib import Path

path = Path("tools/remove_confirmed_dead_audio_apis.py")
text = path.read_text(encoding="utf-8")
old = "        if symbol in text:\n            found.add(path.relative_to(ROOT).as_posix())"
new = "        if f\"{symbol}(\" in text:\n            found.add(path.relative_to(ROOT).as_posix())"
if text.count(old) != 1:
    raise RuntimeError("initial symbol scan block mismatch")
text = text.replace(old, new, 1)
old = "        if symbol in content:\n            raise RuntimeError(f\"{symbol} remains in {rel}\")"
new = "        if f\"{symbol}(\" in content:\n            raise RuntimeError(f\"{symbol} remains in {rel}\")"
if text.count(old) != 1:
    raise RuntimeError("final symbol scan block mismatch")
text = text.replace(old, new, 1)
path.write_text(text, encoding="utf-8")
print("dead API audit now matches exact function tokens")
