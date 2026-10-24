#!/usr/bin/env python3
"""Package ordinary RAK CI output or validate a tracked publication request."""

import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from verify_snorr_release import checksum_text, verify_assets, verify_source

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "lfg-seth/firmware"
TAG_PATTERN = r"snorr-rak4631-\d+\.\d+\.\d+\.[0-9a-f]{7}-pre\.1"


def git(*arguments):
    return subprocess.check_output(["git", *arguments], cwd=ROOT, text=True).strip()


def output(**values):
    with open(os.environ["GITHUB_OUTPUT"], "a") as stream:
        for key, value in values.items():
            stream.write(f"{key}={value}\n")


def select():
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    before = event["before"]
    if before == "0" * 40:
        changed = git(
            "diff-tree", "--no-commit-id", "--no-renames", "--name-status", "-r", "HEAD"
        )
    else:
        changed = git("diff", "--no-renames", "--name-status", before, "HEAD")
    entries = [line.split("\t") for line in changed.splitlines() if line]
    manifests = [
        (status, path)
        for status, path in entries
        if path.startswith(".github/releases/") and path.endswith(".json")
    ]
    if not manifests:
        output(mode="build")
        return
    if len(manifests) != 1 or manifests[0][0] != "A":
        raise ValueError(
            "Publish exactly one newly added release manifest; never replace a released manifest"
        )
    manifest_path = manifests[0][1]
    if len(entries) != 1:
        raise ValueError("Publication commit must add only the release manifest")
    manifest = json.loads((ROOT / manifest_path).read_text())
    validate_manifest(manifest)
    if manifest_path != f".github/releases/{manifest['tag']}.json":
        raise ValueError("Manifest filename differs from its tag")
    verify_source(manifest)
    output(
        mode="publish",
        tag=manifest["tag"],
        run_id=manifest["ciBuild"]["runId"],
        artifact=manifest["ciBuild"]["artifactName"],
        source=manifest["source"],
    )


def validate_manifest(manifest):
    if (
        manifest["schemaVersion"] != 1
        or manifest["repository"] != REPOSITORY
        or manifest["target"] != "rak4631"
        or not re.fullmatch(TAG_PATTERN, manifest["tag"])
        or not re.fullmatch(r"[0-9a-f]{40}", manifest["source"])
        or manifest["tag"] != f"snorr-rak4631-{manifest['version']}-pre.1"
        or manifest["version"].split(".")[-1] != manifest["source"][:7]
        or manifest["snorrNeighborLoraIntervalSeconds"] != 180
        or manifest["hardwareVerified"] is not False
    ):
        raise ValueError("Invalid ordinary RAK source/version/cadence manifest")
    build = manifest["ciBuild"]
    if (
        build["provider"] != "github-actions"
        or not isinstance(build["runId"], int)
        or build["runId"] <= 0
        or not isinstance(build["runAttempt"], int)
        or build["runAttempt"] <= 0
        or build["artifactName"] != "snorr-rak4631-" + manifest["source"]
    ):
        raise ValueError("Invalid exact CI artifact pointer")


def package(destination):
    source = git("rev-parse", "HEAD")
    if git("diff", "HEAD", "--"):
        raise ValueError("Refusing dirty tracked source")
    configuration = configparser.ConfigParser(interpolation=None)
    configuration.read(ROOT / "variants/nrf52840/rak4631/platformio.ini")
    flags = configuration["env:rak4631"]["build_flags"]
    if (
        "USERPREFS_SNORR_NEIGHBOR_LORA_INTERVAL_SECS=180" not in flags
        or "SNORR_INA219_DIAGNOSTICS" in flags
        or "USERPREFS_SNORR_DUMMY_LOAD" in flags
    ):
        raise ValueError("Expected ordinary RAK with 180-second SNNS cadence")
    version = subprocess.check_output(
        ["python3", "bin/buildinfo.py", "long"], cwd=ROOT, text=True
    ).strip()
    manifest = {
        "schemaVersion": 1,
        "repository": REPOSITORY,
        "tag": f"snorr-rak4631-{version}-pre.1",
        "target": "rak4631",
        "version": version,
        "source": source,
        "hardwareVerified": False,
        "snorrNeighborLoraIntervalSeconds": 180,
        "ciBuild": {
            "provider": "github-actions",
            "runId": int(os.environ["GITHUB_RUN_ID"]),
            "runAttempt": int(os.environ["GITHUB_RUN_ATTEMPT"]),
            "artifactName": "snorr-rak4631-" + source,
        },
        "artifacts": [],
    }
    validate_manifest(manifest)
    destination.mkdir(parents=True, exist_ok=False)
    for extension in ["uf2", "zip"]:
        original_name = f"firmware-rak4631-{version}.{extension}"
        name = (
            f"firmware-rak4631-{version}-ota.zip"
            if extension == "zip"
            else original_name
        )
        original = ROOT / ".pio/build/rak4631" / original_name
        data = original.read_bytes()
        if not data:
            raise ValueError("Empty firmware asset")
        shutil.copyfile(original, destination / name)
        manifest["artifacts"].append(
            {
                "name": name,
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    manifest_bytes = (json.dumps(manifest, indent=2) + "\n").encode()
    (destination / "build-manifest.json").write_bytes(manifest_bytes)
    (destination / "SHA256SUMS").write_bytes(checksum_text(manifest).encode())
    verify_assets(destination, manifest_bytes)
    output(artifact=manifest["ciBuild"]["artifactName"], tag=manifest["tag"])
    print("BEGIN_SNORR_BUILD_MANIFEST")
    print(manifest_bytes.decode(), end="")
    print("END_SNORR_BUILD_MANIFEST")
    print(f"MANIFEST_BYTES={len(manifest_bytes)}")
    print(f"MANIFEST_SHA256={hashlib.sha256(manifest_bytes).hexdigest()}")


def validate_run(path, tag):
    run = json.loads(path.read_text())
    manifest = json.loads((ROOT / ".github/releases" / f"{tag}.json").read_text())
    validate_manifest(manifest)
    if (
        run["repository"]["full_name"] != REPOSITORY
        or run["head_repository"]["full_name"] != REPOSITORY
        or run["head_branch"] != "snorr/rak-survey"
        or run["head_sha"] != manifest["source"]
        or run["event"] != "push"
        or run["conclusion"] != "success"
        or run["path"] != ".github/workflows/snorr_rak_release.yml"
        or run["id"] != manifest["ciBuild"]["runId"]
        or run["run_attempt"] != manifest["ciBuild"]["runAttempt"]
    ):
        raise ValueError("Prior artifact run is not the exact successful source build")
    verify_source(manifest)
    print("Exact prior source-build run and tracked source lineage PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument("--select", action="store_true")
    actions.add_argument("--package", type=Path)
    actions.add_argument("--validate-run", type=Path)
    parser.add_argument("--tag")
    args = parser.parse_args()
    if args.select:
        select()
    elif args.package:
        package(args.package)
    else:
        if not args.tag or not re.fullmatch(TAG_PATTERN, args.tag):
            parser.error("--validate-run requires a valid --tag")
        validate_run(args.validate_run, args.tag)


if __name__ == "__main__":
    main()
