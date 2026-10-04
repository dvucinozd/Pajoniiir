"""Verify board isolation, pinned LVGL, app identity and image budget after IDF build."""
import argparse
import json
from pathlib import Path
import struct
import re


def verify(build: Path, project: str) -> None:
    description = json.loads((build / "project_description.json").read_text())
    assert description["project_name"] == project, "wrong configured project"
    assert description["target"] == "esp32p4", "wrong silicon target"
    components = set(description["build_components"])
    jc1060 = project == "main-deck-jc1060"
    assert ("bsp_jc1060" in components) == jc1060, "wrong display BSP"
    assert ("bsp_jc4880" in components) != jc1060, "wrong display BSP"
    assert ("board_ethernet" in components) == jc1060, "wrong Ethernet transport"
    assert {"board_adapter", "audio_engine", "deck_core", "library", "ui"} <= components
    config = (build / "config/sdkconfig.h").read_text()
    assert ("#define CONFIG_PAJONIIIR_BOARD_JC1060 1" in config) == jc1060
    if jc1060:
        assert "#define CONFIG_BSP_PCM5102A_MAIN_OUT 1" not in config, "Ethernet pin conflict"
        assert "#define CONFIG_BSP_ES8311_MONITOR 1" not in config, "USB-only sink must not use codec pacing"
    # CI builds inside /project in Docker, then checks from the runner host.
    # The recorded container path is not a filesystem path on that host.
    project_path = build.resolve().parent
    lock = (project_path / "dependencies.lock").read_text()
    lvgl = re.search(r"^  lvgl/lvgl:\n((?:    .*\n|\n)*)", lock, re.MULTILINE)
    assert lvgl and "version: 9.5.0" in lvgl.group(1), "LVGL version drift"
    image = (build / f"{project}.bin").read_bytes()
    assert len(image) <= 0x380000, "application exceeds fixed budget"
    assert struct.unpack_from("<I", image, 32)[0] == 0xABCD5432, "missing app descriptor"
    embedded_project = image[80:112].split(b"\0", 1)[0].decode()
    assert embedded_project == project, "wrong embedded OTA project identity"
    print(f"PASS {project}: {len(image)} bytes, isolated BSP, LVGL 9.5.0, app identity")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--project", choices=["main-deck-p4", "main-deck-jc1060"], required=True)
    args = parser.parse_args()
    verify(args.build, args.project)
