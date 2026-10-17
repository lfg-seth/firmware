import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from verify_snorr_release import checksum_text, verify_assets


class ReleaseAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        payload = b"cached firmware fixture"
        self.filename = "firmware-rak4631-fixture.uf2"
        self.manifest = {
            "artifacts": [
                {
                    "name": self.filename,
                    "bytes": len(payload),
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }
            ]
        }
        self.manifest_bytes = (json.dumps(self.manifest) + "\n").encode()
        (self.directory / self.filename).write_bytes(payload)
        (self.directory / "build-manifest.json").write_bytes(self.manifest_bytes)
        (self.directory / "SHA256SUMS").write_text(
            checksum_text(self.manifest), encoding="utf-8", newline="\n"
        )

    def test_exact_assets(self):
        verify_assets(self.directory, self.manifest_bytes)

    def test_same_length_corruption(self):
        path = self.directory / self.filename
        path.write_bytes(b"X" + path.read_bytes()[1:])
        with self.assertRaisesRegex(ValueError, "SHA256"):
            verify_assets(self.directory, self.manifest_bytes)

    def test_missing_asset(self):
        (self.directory / self.filename).unlink()
        with self.assertRaisesRegex(ValueError, "asset set"):
            verify_assets(self.directory, self.manifest_bytes)

    def test_unexpected_asset(self):
        (self.directory / "unintended-file").write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, "asset set"):
            verify_assets(self.directory, self.manifest_bytes)

    def test_replaced_manifest(self):
        (self.directory / "build-manifest.json").write_bytes(b"{}")
        with self.assertRaisesRegex(ValueError, "manifest differs"):
            verify_assets(self.directory, self.manifest_bytes)

    def test_replaced_checksum_list(self):
        (self.directory / "SHA256SUMS").write_bytes(b"incorrect")
        with self.assertRaisesRegex(ValueError, "Checksum list"):
            verify_assets(self.directory, self.manifest_bytes)


if __name__ == "__main__":
    unittest.main()
