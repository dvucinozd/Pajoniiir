"""Verify board isolation, pinned LVGL, app identity and image budget after IDF build."""
import argparse
import json
from pathlib import Path
import struct
import re
import subprocess


def verify(build: Path, project: str, experimental_recorder: bool = False,
           experimental_link: bool = False) -> None:
    description = json.loads((build / "project_description.json").read_text())
    assert description["project_name"] == project, "wrong configured project"
    assert description["target"] == "esp32p4", "wrong silicon target"
    components = set(description["build_components"])
    jc1060 = project == "main-deck-jc1060"
    m3 = project == "main-deck-m3"
    selected_bsp = {"main-deck-p4": "bsp_jc4880", "main-deck-jc1060": "bsp_jc1060", "main-deck-m3": "bsp_p4_m3"}[project]
    assert components & {"bsp_jc4880", "bsp_jc1060", "bsp_p4_m3"} == {selected_bsp}, "exactly one matching BSP required"
    assert ("board_ethernet" in components) == jc1060, "wrong Ethernet transport"
    for link_component in ("djlink", "dj_link_core", "dj_link_service"):
        assert link_component in components, "shared network component missing"
    assert {"board_adapter", "audio_engine", "deck_core", "library", "ui"} <= components
    # Build directories may be build_recorder/build_link; their parent remains
    # the board entrypoint. Every portable component in the graph must come
    # from the canonical core, including optional archives that GC later drops.
    core = build.resolve().parent.parent / "p4-core/components"
    names = set(path.name for path in core.iterdir() if (path / "CMakeLists.txt").is_file())
    required_core = {"p4_app", "board_adapter", "audio_engine", "deck_core", "library", "ui",
                     "web_server", "p4_ota", "controller_profile_manager", "controller_profile_runtime",
                     "controller_usb_host", "usb_storage", "usb_host_manager", "audio_recorder"}
    assert required_core <= components, "production shared component missing"
    for name, path in zip(description["build_components"], description["build_component_paths"]):
        if name in names:
            assert path.replace("\\", "/").endswith(f"/p4-core/components/{name}"), f"private copy of {name}"
    config = (build / "config/sdkconfig.h").read_text()
    link_enabled = "#define CONFIG_PAJONIIIR_DJ_LINK_SERVICE 1" in config
    assert link_enabled == (jc1060 or experimental_link), "unqualified network service in ordinary build"
    link_index = description["build_components"].index("dj_link_service")
    link_path = description["build_component_paths"][link_index].replace("\\", "/")
    assert link_path.endswith("/p4-core/components/dj_link_service"), "network service is not shared"
    assert "#define CONFIG_LV_USE_CUSTOM_MALLOC 1" in config, "UI must keep LVGL allocations in PSRAM"
    assert "#define CONFIG_LV_USE_CLIB_MALLOC 1" not in config, "UI cannot consume internal heap for small objects"
    recorder = "#define CONFIG_AUDIO_RECORDER_ENABLED 1" in config
    experiment = "#define CONFIG_AUDIO_RECORDER_EXPERIMENTAL_BUILD 1" in config
    assert recorder == experiment == experimental_recorder, "experimental recorder in wrong build class"
    if not experimental_recorder:
        assert "#define CONFIG_PAJONIIIR_SD_IDLE_WAIT 1" not in config, "unqualified SD experiment in ordinary build"
    psram_dma = "#define CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM 1" in config
    if psram_dma:
        assert experimental_recorder, "PSRAM DMA is an unqualified storage experiment"
        assert jc1060, "JC4880 USB DMA policy must stay internal"
        assert "#define CONFIG_PAJONIIIR_SD_INTERNAL_BOUNCE 1" in config, "PSRAM USB DMA requires internal SD bounce"
    assert ("#define CONFIG_PAJONIIIR_BOARD_JC1060 1" in config) == jc1060
    assert ("#define CONFIG_PAJONIIIR_BOARD_M3 1" in config) == m3
    assert ("#define CONFIG_AUDIO_ANTIALIAS_CACHE 1" in config) == m3, "wrong persistent FIR memory policy"
    if m3:
        assert "#define CONFIG_BSP_PCM5102A_MAIN_OUT 1" in config, "M3 MAIN must use PCM5102A"
        assert "#define CONFIG_BSP_ES8311_MONITOR 1" not in config, "M3 monitor codec is retired"
        assert "#define CONFIG_PAJONIIIR_DEFAULT_WIFI_REMOTE 1" in config, "M3 defaults must keep Wi-Fi enabled"
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
    identity = json.loads((build / "board_build_identity.json").read_text())
    assert re.fullmatch(r"[0-9a-f]{40}", identity["source_sha"]), "invalid compiled source SHA"
    assert identity["source_dirty"] in (0, 1), "invalid source cleanliness flag"
    assert identity["source_sha"].encode() in image, "source metadata not bound into the application"
    root = project_path.parent.parent
    current_sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(["git", "-C", str(root), "status", "--porcelain"], text=True).strip())
    assert identity == {"source_sha": current_sha, "source_dirty": int(dirty)}, "stale build identity; reconfigure and rebuild"
    print(f"PASS {project}: {len(image)} bytes, isolated BSP, LVGL 9.5.0, app identity")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--project", choices=["main-deck-p4", "main-deck-jc1060", "main-deck-m3"], required=True)
    parser.add_argument("--experimental-recorder", action="store_true")
    parser.add_argument("--experimental-link", action="store_true")
    args = parser.parse_args()
    verify(args.build, args.project, args.experimental_recorder, args.experimental_link)
