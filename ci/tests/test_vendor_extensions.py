"""Release fixtures for SDK selection and archive integrity."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import vendor_extensions
from common import CIError


class VendorPayload(unittest.TestCase):
    def setUp(self):
        self.workspace = tempfile.TemporaryDirectory()
        self.addCleanup(self.workspace.cleanup)
        self.root = Path(self.workspace.name)
        self.cache = self.root / "cache"
        self.cache.mkdir()
        self.manifest = """[configuration]
entry_symbol = "vendor_init"
[libraries]
linux.debug.x86_64 = "res://addons/sdk/linux/library.so"
linux.debug.arm64 = "res://addons/sdk/arm/library.so"
windows.debug.x86_64 = "res://addons/sdk/win/library.dll"
macos.debug = "res://addons/sdk/mac/library.dylib"
[dependencies]
linux.x86_64 = { "res://addons/sdk/linux/api.so": "" }
"""
        self.entries = {
            "addons/sdk/vendor.gdextension": self.manifest.encode(),
            "addons/sdk/linux/library.so": b"linux",
            "addons/sdk/linux/api.so": b"dependency",
            "addons/sdk/arm/library.so": b"unsupported",
            "addons/sdk/win/library.dll": b"windows",
            "addons/sdk/mac/library.dylib": b"macos",
            "addons/sdk/LICENSE": b"license",
            "addons/sdk/editor.gd": b"unneeded",
        }

    def assemble(self):
        archive = self.cache / "input.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            for name, data in self.entries.items():
                bundle.writestr(name, data)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.rename(self.cache / (digest + ".zip"))
        pin = {
            "version": "1",
            "sha256": digest,
            "url": "https://example.invalid/sdk.zip",
            "prefix": "addons/sdk/",
            "destination": "addons/sdk",
            "manifest": "vendor.gdextension",
            "source": "https://example.invalid/source",
        }
        with patch.object(
            vendor_extensions,
            "load_json",
            return_value={
                "sdk": pin,
            },
        ):
            return vendor_extensions.place(self.root / "stage", self.cache)

    def test_desktop_libraries_keep_their_dependencies_and_licenses(self):
        records = self.assemble()
        target = self.root / "stage/addons/sdk"
        self.assertTrue((target / "linux/api.so").is_file())
        self.assertTrue((target / "LICENSE").is_file())
        self.assertTrue((target / "win/library.dll").is_file())
        self.assertTrue((target / "mac/library.dylib").is_file())
        self.assertFalse((target / "arm").exists())
        self.assertFalse((target / "editor.gd").exists())
        self.assertNotIn("arm64", (target / "vendor.gdextension").read_text())
        self.assertEqual(records["sdk"]["version"], "1")
        self.assertIn("source", (target / "SOURCES.txt").read_text())

    def test_a_missing_redistributable_refuses_the_package(self):
        del self.entries["addons/sdk/linux/api.so"]
        with self.assertRaisesRegex(CIError, "missing linux/api.so"):
            self.assemble()

    def test_an_archive_cannot_escape_its_destination(self):
        self.entries["addons/sdk/../../escape"] = b"unsafe"
        with self.assertRaisesRegex(CIError, "unsafe path"):
            self.assemble()

    def test_a_bad_checksum_refuses_the_package(self):
        self.assemble()
        archive = next(self.cache.glob("*.zip"))
        archive.write_bytes(b"corrupt")
        pin = {
            "sha256": archive.stem,
            "url": "https://example.invalid/sdk.zip",
        }
        with patch.object(
            vendor_extensions,
            "load_json",
            return_value={
                "sdk": pin,
            },
        ):
            with self.assertRaisesRegex(CIError, "SHA-256"):
                vendor_extensions.place(self.root / "stage", self.cache)


if __name__ == "__main__":
    unittest.main()
