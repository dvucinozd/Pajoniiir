#!/usr/bin/env python3
from pathlib import Path

path = Path("firmware/main-deck-p4/CLAUDE.md")
text = path.read_text(encoding="utf-8")


def replace_between(source: str, start: str, end: str, replacement: str, label: str) -> str:
    start_at = source.find(start)
    if start_at < 0:
        raise RuntimeError(f"{label}: start marker not found")
    end_at = source.find(end, start_at)
    if end_at < 0:
        raise RuntimeError(f"{label}: end marker not found")
    return source[:start_at] + replacement.rstrip() + source[end_at:]


text = replace_between(
    text,
    "Documentation status:",
    "\n\n> ⚠️ **Web assets must not reference the network.**",
    """Documentation status: current ESP-IDF 6.0.2 developer guide, refreshed after
    the `fixevi.md` remediation audit. The release branch now uses transactional
    media loading, strict ANLZ parsing, actor-owned deck snapshots, bounded S3 Debug
    AP/SSE handling, USB desired/current reconciliation, a shared Wi-Fi transition
    lease, and a single real DPI framebuffer. The master-output recorder is
    deliberately release-disabled until its STOP/finalize safety work is complete.

    Software acceptance is enforced by `.github/workflows/esp-idf-6-migration.yml`:
    host regressions plus clean ESP32-S3 and ESP32-P4 builds using ESP-IDF 6.0.2.
    Physical release checks remain tracked in `docs/fixevi-remediation-audit.md` and
    PR #8 rather than being inferred from a successful build.""",
    "documentation status",
)

text = replace_between(
    text,
    "The latest full\nfunctional hardware acceptance remains",
    "\n\n## Project Overview",
    """Historical hardware acceptance records remain useful as bench evidence, but
    they are not a substitute for the current release gates. Use
    `docs/fixevi-remediation-audit.md` and PR #8 for the authoritative pending
    hardware list. The repository is `dvucinozd/Pajoniiir`; active migration and
    remediation branches coexist with the default branch, while retired work may be
    archived under `attic/*` tags.""",
    "hardware acceptance status",
)

text = text.replace(
    "SDMMC mount, display triple buffering, and the",
    "SDMMC mount, a single DPI framebuffer fed by the partial LVGL/PPA path, and the",
    1,
)
text = text.replace(
    "; section-header tag walk; 34 unit tests",
    "; strict bounded section walk; host regression coverage",
    1,
)
text = text.replace(
    "PC unit tests (`tests/anlz/`): via `.\\tests\\run_p4_host_tests.ps1` → 34/34 PASS",
    "PC unit tests (`tests/anlz/`) are included in `.\\tests\\run_p4_host_tests_current.ps1`.",
    1,
)
text = text.replace(
    "run all P4 host tests via\n`.\\tests\\run_p4_host_tests.ps1`",
    "run all P4 host tests via\n`.\\tests\\run_p4_host_tests_current.ps1`",
    1,
)

text = replace_between(
    text,
    "**ESP-IDF environments**:",
    "\n\n> **Sound is in the default build",
    """**ESP-IDF environment:** ESP-IDF **6.0.2**. CI uses
    `espressif/idf:v6.0.2`; a local shell must report the same release from
    `idf.py --version` before configuring or flashing this firmware.

    > ⚠️ **The production board uses pre-v3 ESP32-P4 silicon (board-observed v1.3).**
    > `sdkconfig.defaults` must retain all three selectors below. Omitting the
    > pre-v3 selector allows a clean configuration to target revision 3.1, which
    > will not boot on the production board.
    >
    > ```text
    > CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
    > CONFIG_ESP32P4_REV_MIN_100=y
    > CONFIG_ESP32P4_REV_MIN_FULL=100
    > ```

    ```powershell
    # Activate an ESP-IDF 6.0.2 shell first.
    idf.py --version
    cd firmware/main-deck-p4
    Remove-Item -Recurse -Force build,sdkconfig,sdkconfig.old -ErrorAction SilentlyContinue
    idf.py set-target esp32p4
    idf.py reconfigure
    idf.py build
    idf.py -p COM15 flash
    ```""",
    "build environment",
)

text = replace_between(
    text,
    "**Rotation (PPA):**",
    "\n\n**Why NOT LVGL sw-rotation:**",
    """**Rotation (PPA):** `ui_lvgl_backend_single_fb.c` creates the LVGL display as
    800×480 landscape without `lv_display_set_rotation`. LVGL renders partial rows
    into one cache-aligned PSRAM draw buffer; the flush callback uses
    `ppa_do_scale_rotate_mirror()` at `PPA_SRM_ROTATION_ANGLE_270` to write directly
    into the single 480×800 DPI framebuffer. There is no inactive-buffer swap in
    the current backend. BSP and UI share `BSP_LCD_FRAMEBUFFER_COUNT == 1`, enforced
    by compile-time assertions. `esp_cache_msync(C2M)` is still required before PPA
    reads the PSRAM render buffer.""",
    "display rotation",
)

text = text.replace(
    "The same bus will be shared by the ES8311 codec (`bsp_get_i2c_bus()`).",
    "The ES8311 path is disabled in product defaults; MAIN audio uses PCM5102A and cue audio uses the FLX4 USB path. `bsp_get_i2c_bus()` remains the shared board I2C accessor.",
    1,
)

stale = [
    "esp-idf-v5.5",
    "v5.5.4",
    "CONFIG_ESP32P4_REV_MIN_FULL=0",
    "display triple buffering",
    "inactive DPI frame buffer",
    "3 DPI frame buffers",
    "FULL render mode, 2 PSRAM render buffers",
    "The same bus will be shared by the ES8311 codec",
    ".\\tests\\run_p4_host_tests.ps1",
]
remaining = [marker for marker in stale if marker in text]
if remaining:
    raise RuntimeError("stale documentation markers remain: " + ", ".join(remaining))

path.write_text(text, encoding="utf-8")
print("CLAUDE.md refreshed for ESP-IDF 6.0.2 and single-framebuffer runtime")
