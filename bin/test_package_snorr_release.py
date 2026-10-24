#!/usr/bin/env python3
"""Check CI artifact provenance and immutable publication selection."""

import contextlib
import copy
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import package_snorr_release as package

SOURCE = "abcdef1" + "0" * 33
TAG = "snorr-rak4631-2.7.26.abcdef1-pre.1"


def manifest():
    return {
        "schemaVersion": 1,
        "repository": "lfg-seth/firmware",
        "target": "rak4631",
        "tag": TAG,
        "version": "2.7.26.abcdef1",
        "source": SOURCE,
        "snorrNeighborLoraIntervalSeconds": 180,
        "hardwareVerified": False,
        "ciBuild": {
            "provider": "github-actions",
            "runId": 123,
            "runAttempt": 1,
            "artifactName": "snorr-rak4631-" + SOURCE,
        },
        "artifacts": [],
    }


class PublicationTest(unittest.TestCase):
    def test_rejects_wrong_source_cadence_and_artifact_pointer(self):
        for path, value in [
            ("source", "b" * 40),
            ("snorrNeighborLoraIntervalSeconds", 300),
            ("hardwareVerified", True),
            ("target", "rak4631-ina219-debug"),
        ]:
            invalid = manifest()
            invalid[path] = value
            with self.subTest(path=path), self.assertRaises(ValueError):
                package.validate_manifest(invalid)
        invalid = manifest()
        invalid["ciBuild"]["artifactName"] = "another-build"
        with self.assertRaises(ValueError):
            package.validate_manifest(invalid)

    def test_rejects_modified_manifest_and_mixed_publication_commit(self):
        with tempfile.TemporaryDirectory() as directory:
            event = Path(directory) / "event.json"
            event.write_text(json.dumps({"before": "1" * 40}))
            with patch.dict(os.environ, {"GITHUB_EVENT_PATH": str(event)}):
                for changed in [
                    f"M\t.github/releases/{TAG}.json",
                    f"A\t.github/releases/{TAG}.json\nM\tsrc/main.cpp",
                ]:
                    with patch.object(
                        package, "git", return_value=changed
                    ), self.assertRaises(ValueError):
                        package.select()

    def test_rejects_wrong_or_unsuccessful_prior_build(self):
        expected_run = {
            "repository": {"full_name": "lfg-seth/firmware"},
            "head_repository": {"full_name": "lfg-seth/firmware"},
            "head_branch": "snorr/rak-survey",
            "head_sha": SOURCE,
            "event": "push",
            "conclusion": "success",
            "path": ".github/workflows/snorr_rak_release.yml",
            "id": 123,
            "run_attempt": 1,
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            releases = root / ".github/releases"
            releases.mkdir(parents=True)
            (releases / f"{TAG}.json").write_text(json.dumps(manifest()))
            run_path = root / "run.json"
            with patch.object(package, "ROOT", root), patch.object(
                package, "verify_source"
            ):
                for field, value in [
                    ("head_sha", "b" * 40),
                    ("head_branch", "main"),
                    ("conclusion", "failure"),
                    ("id", 124),
                    ("run_attempt", 2),
                ]:
                    invalid = copy.deepcopy(expected_run)
                    invalid[field] = value
                    run_path.write_text(json.dumps(invalid))
                    with self.subTest(field=field), self.assertRaises(ValueError):
                        package.validate_run(run_path, TAG)
                run_path.write_text(json.dumps(expected_run))
                with contextlib.redirect_stdout(io.StringIO()):
                    package.validate_run(run_path, TAG)

    def test_package_logs_exact_manifest_and_four_verified_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            variant = root / "variants/nrf52840/rak4631"
            variant.mkdir(parents=True)
            (variant / "platformio.ini").write_text(
                "[env:rak4631]\nbuild_flags = -DUSERPREFS_SNORR_NEIGHBOR_LORA_INTERVAL_SECS=180\n"
            )
            build = root / ".pio/build/rak4631"
            build.mkdir(parents=True)
            for extension in ["uf2", "zip"]:
                (build / f"firmware-rak4631-2.7.26.abcdef1.{extension}").write_bytes(
                    extension.encode()
                )
            output = root / "github-output"
            env = {
                "GITHUB_OUTPUT": str(output),
                "GITHUB_RUN_ID": "123",
                "GITHUB_RUN_ATTEMPT": "1",
            }
            captured = io.StringIO()
            with patch.object(package, "ROOT", root), patch.object(
                package, "git", side_effect=[SOURCE, ""]
            ), patch.object(
                package.subprocess, "check_output", return_value="2.7.26.abcdef1\n"
            ), patch.dict(
                os.environ, env
            ), contextlib.redirect_stdout(
                captured
            ):
                package.package(root / "bundle")
            saved = (root / "bundle/build-manifest.json").read_bytes()
            logged = (
                captured.getvalue()
                .split("BEGIN_SNORR_BUILD_MANIFEST\n")[1]
                .split("END_SNORR_BUILD_MANIFEST")[0]
                .encode()
            )
            self.assertEqual(saved, logged)
            self.assertEqual(len(list((root / "bundle").iterdir())), 4)
            self.assertTrue(
                (root / "bundle/firmware-rak4631-2.7.26.abcdef1-ota.zip").is_file()
            )


if __name__ == "__main__":
    unittest.main()
