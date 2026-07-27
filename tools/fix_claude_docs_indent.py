#!/usr/bin/env python3
from pathlib import Path

path = Path("firmware/main-deck-p4/CLAUDE.md")
text = path.read_text(encoding="utf-8")


def normalize_range(source: str, start: str, end: str, label: str) -> str:
    a = source.find(start)
    if a < 0:
        raise RuntimeError(f"{label}: start marker not found")
    b = source.find(end, a)
    if b < 0:
        raise RuntimeError(f"{label}: end marker not found")
    block = source[a:b]
    lines = block.splitlines(keepends=True)
    normalized = []
    for index, line in enumerate(lines):
        if index > 0 and line.startswith("    "):
            line = line[4:]
        normalized.append(line)
    return source[:a] + "".join(normalized) + source[b:]


text = normalize_range(
    text,
    "Documentation status:",
    "\n\n> ⚠️ **Web assets must not reference the network.**",
    "documentation status",
)
text = normalize_range(
    text,
    "Historical hardware acceptance records remain useful",
    "\n\n## Project Overview",
    "hardware acceptance",
)
text = normalize_range(
    text,
    "**ESP-IDF environment:**",
    "\n\n> **Sound is in the default build",
    "build instructions",
)
text = normalize_range(
    text,
    "**Rotation (PPA):**",
    "\n\n**Why NOT LVGL sw-rotation:**",
    "display rotation",
)

for forbidden in (
    "\n    the `fixevi.md` remediation audit",
    "\n    `espressif/idf:v6.0.2`",
    "\n    > ⚠️ **The production board",
    "\n    into the single 480×800 DPI framebuffer",
):
    if forbidden in text:
        raise RuntimeError(f"indented Markdown remains: {forbidden!r}")

path.write_text(text, encoding="utf-8")
print("CLAUDE.md indentation normalized")
