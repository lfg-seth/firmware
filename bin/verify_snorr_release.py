#!/usr/bin/env python3
"""Verify cached SNORR release assets against the tagged, tracked manifest."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.request import Request, urlopen

REPOSITORY = "lfg-seth/firmware"
ROOT = Path(__file__).resolve().parents[1]


def checksum_text(manifest):
    return "".join(
        f"{asset['sha256']}  {asset['name']}\n" for asset in manifest["artifacts"]
    )


def verify_assets(directory, manifest_bytes):
    manifest = json.loads(manifest_bytes)
    expected = {asset["name"] for asset in manifest["artifacts"]} | {
        "build-manifest.json",
        "SHA256SUMS",
    }
    if {path.name for path in directory.iterdir()} != expected:
        raise ValueError("Release asset set differs from the tracked manifest")
    if (directory / "build-manifest.json").read_bytes() != manifest_bytes:
        raise ValueError("Published manifest differs from the tracked manifest")
    if (directory / "SHA256SUMS").read_bytes() != checksum_text(manifest).encode():
        raise ValueError("Checksum list differs from the tracked manifest")
    for asset in manifest["artifacts"]:
        data = (directory / asset["name"]).read_bytes()
        if (
            len(data) != asset["bytes"]
            or hashlib.sha256(data).hexdigest() != asset["sha256"]
        ):
            raise ValueError(f"Size/SHA256 mismatch: {asset['name']}")


def verify_source(manifest):
    changed = subprocess.check_output(
        ["git", "diff", "--name-only", manifest["source"], "HEAD"], cwd=ROOT, text=True
    ).splitlines()
    allowed = {
        ".github/workflows/release_channels.yml",
        "bin/verify_snorr_release.py",
        "bin/test_verify_snorr_release.py",
        "SNORR_RELEASES.md",
        f".github/releases/{manifest['tag']}.json",
    }
    if set(changed) - allowed:
        raise ValueError(
            "Release tag changes firmware/build inputs beyond the cached build source"
        )


def download_release(tag, directory, manifest_bytes):
    manifest = json.loads(manifest_bytes)
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "snorr-release-verifier",
    }
    if os.environ.get("GH_TOKEN"):
        headers["Authorization"] = f"Bearer {os.environ['GH_TOKEN']}"
    request = Request(
        f"https://api.github.com/repos/{REPOSITORY}/releases/tags/{tag}",
        headers=headers,
    )
    with urlopen(request, timeout=30) as response:
        release = json.load(response)
    if release["tag_name"] != tag or release["draft"] or not release["prerelease"]:
        raise ValueError("Expected a published prerelease with the exact tag")
    expected_sizes = {asset["name"]: asset["bytes"] for asset in manifest["artifacts"]}
    expected_sizes["build-manifest.json"] = len(manifest_bytes)
    expected_sizes["SHA256SUMS"] = len(checksum_text(manifest).encode())
    assets = release["assets"]
    if len(assets) != len(expected_sizes) or {asset["name"] for asset in assets} != set(
        expected_sizes
    ):
        raise ValueError(
            "Published release has missing, duplicate or unexpected assets"
        )
    prefix = f"https://github.com/{REPOSITORY}/releases/download/{tag}/"
    for asset in assets:
        name = asset["name"]
        if asset["state"] != "uploaded" or asset["size"] != expected_sizes[name]:
            raise ValueError(f"Incomplete/wrong-sized release asset: {name}")
        if asset["browser_download_url"] != prefix + name:
            raise ValueError("Unexpected release download destination")
        # Public downloads do not receive the API credential, including on CDN redirects.
        with urlopen(asset["browser_download_url"], timeout=60) as response:
            data = response.read(expected_sizes[name] + 1)
        if len(data) != expected_sizes[name]:
            raise ValueError(f"Wrong downloaded length: {name}")
        (directory / name).write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument(
        "--asset-dir",
        type=Path,
        help="Verify staged files locally instead of downloading",
    )
    args = parser.parse_args()
    if not re.fullmatch(r"snorr-rak4631-\d+\.\d+\.\d+\.[0-9a-f]{7}-pre\.\d+", args.tag):
        parser.error("Unsupported SNORR prerelease tag")
    manifest_bytes = (ROOT / ".github" / "releases" / f"{args.tag}.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    if (
        manifest["schemaVersion"] != 1
        or manifest["repository"] != REPOSITORY
        or manifest["tag"] != args.tag
        or manifest["target"] != "rak4631"
    ):
        raise ValueError("Unexpected tracked build manifest")
    verify_source(manifest)
    if args.asset_dir:
        verify_assets(args.asset_dir, manifest_bytes)
    else:
        with tempfile.TemporaryDirectory(prefix="snorr-release-") as temporary:
            directory = Path(temporary)
            download_release(args.tag, directory, manifest_bytes)
            verify_assets(directory, manifest_bytes)
    print(f"PASS: {args.tag}: exact cached artifacts, manifest and SHA256SUMS")


if __name__ == "__main__":
    main()
