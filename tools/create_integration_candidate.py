"""Verify a signed ordinary candidate and freeze software/physical evidence.

This writes a local evidence file, never installs or publishes a channel.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import shutil

from check_board_build import verify as verify_board
from ota_signing import inspect_bundle, _load_public, _raw_verify

PROJECTS = {"main-deck-p4": "JC4880", "main-deck-jc1060": "JC1060", "main-deck-m3": "M3"}


def record(path):
    data = path.read_bytes()
    return {"file": path.name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def image_identity(image):
    if len(image) < 112 or image[0] != 0xe9 or struct.unpack_from("<H", image, 12)[0] != 0x12:
        raise ValueError("not an ESP32-P4 application")
    if struct.unpack_from("<I", image, 32)[0] != 0xabcd5432:
        raise ValueError("missing application descriptor")
    fields = []
    for start in (80, 48):
        field = image[start:start + 32]
        if b"\0" not in field:
            raise ValueError("unterminated application descriptor")
        fields.append(field.split(b"\0", 1)[0].decode("utf-8"))
    return tuple(fields)  # project, version


def verify_artifacts(release, project, public):
    image_path = release / f"{project}.bin"
    image = image_path.read_bytes()
    embedded, version = image_identity(image)
    if embedded != project or len(image) > 0x380000:
        raise ValueError("wrong project or fixed image budget exceeded")
    bundle_path = release / f"{project}.ddjota"
    info = inspect_bundle(bundle_path.read_bytes(), _load_public(public))
    if info["target"] != "p4" or info["chip_id"] != 0x12 or info["project"] != embedded or info["version"] != version:
        raise ValueError("signed manifest and application descriptor mismatch")
    if info["sha256"] != hashlib.sha256(image).hexdigest():
        raise ValueError("packaged image and signed bundle mismatch")
    return version, record(image_path), record(bundle_path)


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def verify_release_manifest(release, public, project, version, image, bundle):
    manifest, signature = release / "manifest.json", release / "manifest.sig"
    payload = manifest.read_bytes()
    _raw_verify(_load_public(public), signature.read_bytes(), payload)
    info = json.loads(payload.decode("utf-8-sig"))
    if info["schema_version"] != 2 or info["release_version"] != version or len(info["targets"]) != 1:
        raise ValueError("wrong release manifest")
    target = info["targets"][0]
    expected = {"target": "p4", "project": project, "file": image["file"],
                "image_elf_sha256": (release / image["file"]).read_bytes()[176:208].hex(),
                "ota_bundle": bundle["file"], "size": image["size"], "sha256": image["sha256"],
                "bundle_size": bundle["size"], "bundle_sha256": bundle["sha256"]}
    if any(target.get(k) != v for k, v in expected.items()):
        raise ValueError("signed release manifest artifact mismatch")
    return record(manifest), record(signature)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--release", required=True, type=Path)
    parser.add_argument("--project", choices=PROJECTS, required=True)
    parser.add_argument("--public-key", required=True, type=Path)
    parser.add_argument("--ci-evidence", required=True, type=Path)
    args = parser.parse_args()
    root, build, release = args.repo_root.resolve(), args.build.resolve(), args.release.resolve()
    if git(root, "status", "--porcelain", "--untracked-files=normal"):
        raise ValueError("candidate source worktree must be clean")
    commit = git(root, "rev-parse", "HEAD")
    if git(root, "rev-parse", "@{upstream}") != commit:
        raise ValueError("candidate source is not the pushed upstream revision")
    identity = json.loads((build / "board_build_identity.json").read_text())
    if identity != {"source_sha": commit, "source_dirty": 0}:
        raise ValueError("stale or dirty compiled source identity")
    version = json.loads((build / "project_description.json").read_text())["project_version"]
    if args.project != "main-deck-m3" and version != git(root, "describe", "--tags", "--dirty", "--exclude", "*-g*", "--match", "M2*"):
        raise ValueError("stale JC version ancestry")
    ci = json.loads(args.ci_evidence.read_text(encoding="utf-8-sig"))
    required = {"Host regression tests"}
    required |= {f"ESP32-P4 M3 shared-core firmware ({v})" for v in ("regular", "recorder", "dj-ui", "link")}
    required |= {f"ESP32-P4 firmware ({v})" for v in ("regular", "recorder", "dj-ui")}
    required |= {f"ESP32-P4 JC1060 firmware ({v})" for v in ("regular", "recorder", "psram", "dj-ui")}
    if ci["headSha"] != commit or ci["conclusion"] != "success" or not required <= {j["name"] for j in ci["jobs"]} or any(j["conclusion"] != "success" for j in ci["jobs"]):
        raise ValueError("exact candidate SHA needs every required successful CI job")
    verify_board(build, args.project)
    config = (build / "config/sdkconfig.h").read_text()
    if any(f"#define CONFIG_DDJ_OTA_{flag} 1" in config
           for flag in ("FORCE_ROLLBACK_TEST", "STARTUP_TIMEOUT_TEST")):
        raise ValueError("OTA fault-injection image is not an ordinary candidate")
    if "#define CONFIG_PAJONIIIR_DJ_OVERVIEW 1" in config:
        raise ValueError("preview is not an ordinary candidate")
    desc = json.loads((build / "project_description.json").read_text())
    if Path(desc["project_path"]).resolve() != root / "firmware" / args.project:
        raise ValueError("build belongs to another source checkout")
    if desc["project_version"] != version:
        raise ValueError("stale build version")
    packaged_version, image, bundle = verify_artifacts(release, args.project, args.public_key)
    if packaged_version != version or record(build / image["file"]) != image:
        raise ValueError("stale or different packaged application")
    idf = (build / image["file"]).read_bytes()[144:176].split(b"\0", 1)[0].decode()
    if idf != "v6.0.2":
        raise ValueError("candidate requires ESP-IDF v6.0.2")
    manifest, signature = verify_release_manifest(release, args.public_key, args.project, version, image, bundle)
    signed_source = json.loads((release / "manifest.json").read_text(encoding="utf-8-sig"))
    if signed_source.get("source_sha") != commit:
        raise ValueError("signed release manifest has another source SHA")
    locks = [record(root / "firmware" / p / "dependencies.lock") | {"project": p} for p in PROJECTS]
    files = [record(build / "config/sdkconfig.h"), record(build / "partition_table/partition-table.bin"),
             record(build / "bootloader/bootloader.bin"), record(build / "project_description.json")]
    wired = release / "wired"
    wired.mkdir(exist_ok=True)
    for source, name in ((build / "bootloader/bootloader.bin", "bootloader.bin"),
                         (build / "partition_table/partition-table.bin", "partition-table.bin"),
                         (build / "ota_data_initial.bin", "ota_data_initial.bin")):
        shutil.copyfile(source, wired / name)
    flash = json.loads((build / "flasher_args.json").read_text())
    expected_offsets = {"0x2000", "0x8000", "0x10000", "0x20000"}
    if set(flash["flash_files"]) != expected_offsets:
        raise ValueError("unexpected initial-install offsets")
    evidence_files = {"0x2000": "wired/bootloader.bin", "0x8000": "wired/partition-table.bin",
                      "0x10000": "wired/ota_data_initial.bin", "0x20000": image["file"]}
    evidence = {
        "schema": "pajoniiir.integration-candidate.v1", "source_sha": commit,
        "version": version, "project": args.project, "board": PROJECTS[args.project],
        "idf": "6.0.2", "application_budget": 0x380000, "ota_slot_size": 0x400000,
        "software_verified": True, "hardware_accepted": False, "released": False,
        "ci": {"url": ci["url"], "head_sha": ci["headSha"], "jobs": len(ci["jobs"])},
        "image": image, "bundle": bundle, "locks": locks, "build_files": files,
        "image_elf_sha256": (build / image["file"]).read_bytes()[176:208].hex(),
        "release_manifest": manifest, "release_signature": signature,
        "initial_wired_install": {"flash_files": evidence_files,
            "flash_settings": flash["flash_settings"],
            "files": [record(wired / name) for name in ("bootloader.bin", "partition-table.bin", "ota_data_initial.bin")],
            "performed": False},
        "physical_gates": {g: "NOT RUN" for g in (
            "startup_resources", "touch_render", "controller_midi_led", "main_cue_audio",
            "seek_cue_scratch_loop", "signed_ota_startup_rollback", "campaign_a_30_cycles",
            "campaign_b_worst_case_60_minutes", "operator_audio_ui_confirmation",
            *( ("ethernet_peer_browse_download", "network_sync_phase_handoff") if args.project == "main-deck-jc1060" else () ))},
    }
    output = release / "candidate-evidence.json"
    output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"PASS signed {args.project} candidate {version}: {output}")


if __name__ == "__main__":
    main()
