"""Fixtures for the failures that otherwise reach a release as a green run."""

from __future__ import annotations

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import package  # noqa: E402
import release  # noqa: E402
from common import CIError, addon_version  # noqa: E402
from matrix import load_catalog, resolve  # noqa: E402

CATALOG = load_catalog()
VERSION = addon_version()
TAG = "v%s" % VERSION


def cell(cell_id: str) -> Cell:
    return resolve(CATALOG, cells=[cell_id])[0]


class MatrixRequests(unittest.TestCase):
    def test_the_release_profile_is_the_declared_matrix(self):
        cells = resolve(CATALOG, profile="release")
        self.assertEqual(len(cells), 22)
        self.assertEqual(
            sorted({c.platform for c in cells}),
            ["android", "ios", "linux", "macos", "web", "windows"],
        )

    def test_an_unknown_platform_is_refused(self):
        with self.assertRaises(CIError):
            resolve(CATALOG, platforms=["playstation"])

    def test_an_unknown_slice_is_refused(self):
        with self.assertRaises(CIError):
            resolve(CATALOG, cells=["linux:sparc:template_debug"])

    def test_an_unbuilt_target_is_refused(self):
        with self.assertRaises(CIError):
            resolve(CATALOG, cells=["linux:x86_64:editor"])

    def test_a_duplicate_cell_is_refused(self):
        with self.assertRaises(CIError):
            resolve(CATALOG, cells=["linux:x86_64:template_debug"] * 2)

    def test_an_empty_request_is_refused(self):
        with self.assertRaises(CIError):
            resolve(CATALOG)

    def test_a_malformed_cell_id_is_refused(self):
        with self.assertRaises(CIError):
            resolve(CATALOG, cells=["linux/x86_64/template_debug"])

    def test_the_suffix_matches_what_the_manifest_declares(self):
        manifest = (Path(__file__).resolve().parents[2] / "extension" / "networked.gdextension").read_text()
        for cell_id, expected in (
            ("linux:x86_64:template_debug", "libnetworked.linux.template_debug.x86_64.so"),
            ("windows:x86_64:template_release", "libnetworked.windows.template_release.x86_64.dll"),
            ("android:arm64:template_debug", "libnetworked.android.template_debug.arm64.so"),
            ("web:wasm32:template_release", "libnetworked.web.template_release.wasm32.nothreads.wasm"),
        ):
            with self.subTest(cell_id):
                self.assertEqual(cell(cell_id).outputs, [expected])
                self.assertIn(expected, manifest)

    def test_the_apple_shapes_are_what_the_manifest_promises(self):
        manifest = (Path(__file__).resolve().parents[2] / "extension" / "networked.gdextension").read_text()
        self.assertEqual(
            cell("macos:arm64:template_debug").outputs,
            ["libnetworked.macos.template_debug.framework"],
        )
        self.assertIn("libnetworked.macos.template_debug.framework", manifest)
        self.assertIn("libnetworked.ios.template_debug.xcframework", manifest)
        self.assertEqual(
            cell("ios:arm64-simulator:template_debug").outputs,
            ["libnetworked.ios.template_debug.arm64.simulator.dylib"],
        )

    def test_the_web_cell_carries_its_threading_flag(self):
        web = cell("web:wasm32:template_release")
        self.assertIn("threads=no", web.scons_args)
        self.assertFalse(web.threads)


class ShippedManifest(unittest.TestCase):
    def setUp(self):
        self.text = package.shipped_manifest()
        self.entries = {}
        section = ""
        for line in self.text.splitlines():
            line = line.strip()
            if line.startswith("["):
                section = line
                continue
            if section == "[libraries]" and "=" in line:
                key, value = line.split("=", 1)
                self.entries[key.strip()] = value.strip().strip('"')

    def test_it_carries_no_absolute_path(self):
        self.assertNotIn("res://", self.text)

    def test_every_library_path_is_relative_to_the_addon_root(self):
        for key, value in self.entries.items():
            with self.subTest(key):
                self.assertTrue(value.startswith("./bin/"), "%s is %s" % (key, value))

    def test_every_declared_cell_has_an_entry(self):
        named = {Path(value).name for value in self.entries.values()}
        for target in resolve(CATALOG, profile="release"):
            expected = (
                "libnetworked.ios.%s.xcframework" % target.target
                if target.package == "xcframework"
                else target.outputs[0]
            )
            with self.subTest(target.id):
                self.assertIn(expected, named)

    def test_it_keeps_the_configuration_the_repository_builds_against(self):
        dev = (Path(__file__).resolve().parents[2] / "extension" / "networked.gdextension").read_text()
        for field in ("entry_symbol", "compatibility_minimum"):
            with self.subTest(field):
                self.assertEqual(
                    [line for line in dev.splitlines() if line.startswith(field)],
                    [line for line in self.text.splitlines() if line.startswith(field)],
                )

    def test_an_unrelocatable_path_is_refused(self):
        source = Path(__file__).resolve().parents[2] / "extension" / "networked.gdextension"
        original = source.read_text()
        source.write_text(
            original.replace("res://addons/networked/bin/libnetworked.linux", "res://elsewhere/libnetworked.linux")
        )
        try:
            with self.assertRaises(CIError) as caught:
                package.shipped_manifest()
            self.assertIn("absolute path", str(caught.exception))
        finally:
            source.write_text(original)


class StagedCells(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.staging = self.tmp / "cells"

    def stage(self, cell_id: str, *, sha: str = "a" * 40, content: bytes = b"library", outputs=None) -> Cell:
        target = cell(cell_id)
        directory = self.staging / target.artifact
        directory.mkdir(parents=True, exist_ok=True)
        for name in outputs if outputs is not None else target.outputs:
            (directory / name).write_bytes(content)
        (directory / "build.json").write_text(
            json.dumps(
                {
                    "cell": target.to_json(),
                    "source": {"sha": sha, "dirty": False},
                    "toolchain": {"host": "fixture"},
                    "files": {},
                }
            )
        )
        return target

    def test_a_cell_built_at_another_revision_is_refused(self):
        target = self.stage("linux:x86_64:template_debug", sha="b" * 40)
        with self.assertRaises(CIError) as caught:
            package.read_cell_record(self.staging, target, expect_sha="a" * 40)
        self.assertIn("but the package is for", str(caught.exception))

    def test_a_cell_missing_its_declared_output_is_refused(self):
        target = self.stage("linux:x86_64:template_debug", outputs=[])
        with self.assertRaises(CIError) as caught:
            package.read_cell_record(self.staging, target, expect_sha=None)
        self.assertIn("missing its declared output", str(caught.exception))

    def test_a_cell_with_no_build_record_is_refused(self):
        target = cell("linux:x86_64:template_debug")
        (self.staging / target.artifact).mkdir(parents=True)
        with self.assertRaises(CIError):
            package.read_cell_record(self.staging, target, expect_sha=None)

    def test_a_record_for_another_cell_is_refused(self):
        target = self.stage("linux:x86_64:template_debug")
        other = cell("linux:x86_64:template_release")
        directory = self.staging / target.artifact
        record = json.loads((directory / "build.json").read_text())
        record["cell"] = other.to_json()
        (directory / "build.json").write_text(json.dumps(record))
        with self.assertRaises(CIError) as caught:
            package.read_cell_record(self.staging, target, expect_sha=None)
        self.assertIn("holds a record for", str(caught.exception))

    def test_two_cells_claiming_one_filename_with_different_bytes_are_refused(self):
        first = self.stage("linux:x86_64:template_debug", content=b"one")
        destination = self.tmp / "bin"
        destination.mkdir()
        placed: dict[str, str] = {}
        package.place_plain(first, self.staging, destination, placed)

        rebuilt = self.stage("linux:x86_64:template_debug", content=b"two")
        with self.assertRaises(CIError) as caught:
            package.place_plain(rebuilt, self.staging, destination, placed)
        self.assertIn("would overwrite", str(caught.exception))

    def test_a_framework_missing_a_slice_is_refused(self):
        target = self.stage("macos:arm64:template_debug", outputs=[])
        with self.assertRaises(CIError) as caught:
            package.merge_framework([target], self.staging, self.tmp / "bin")
        self.assertIn("framework slice", str(caught.exception))

    def test_an_xcframework_without_a_simulator_slice_is_refused(self):
        device = self.stage("ios:arm64:template_debug")
        with self.assertRaises(CIError) as caught:
            package.assemble_xcframework([device], self.staging, self.tmp / "bin", self.tmp / "work")
        self.assertIn("no simulator slice", str(caught.exception))

    def test_an_xcframework_without_a_device_slice_is_refused(self):
        simulator = self.stage("ios:arm64-simulator:template_debug")
        with self.assertRaises(CIError) as caught:
            package.assemble_xcframework([simulator], self.staging, self.tmp / "bin", self.tmp / "work")
        self.assertIn("no device slice", str(caught.exception))


class Inventory(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)

    def candidate(self, *, cells: list[str], version: str = VERSION, unqualified=None, sums_correct: bool = True):
        manifest = {
            "product": "networked",
            "version": version,
            "source": {"sha": "c" * 40},
            "cells": [{"id": cell_id} for cell_id in cells],
            "unqualified": unqualified or [],
        }
        manifest_path = self.tmp / ("networked-%s.manifest.json" % version)
        manifest_path.write_text(json.dumps(manifest))
        archive = self.tmp / ("networked-%s.zip" % version)
        archive.write_bytes(b"payload")
        sums = self.tmp / "SHA256SUMS.txt"
        from common import digest_of

        lines = []
        for path in (archive, manifest_path):
            digest = digest_of(path) if sums_correct else "0" * 64
            lines.append("%s  %s" % (digest, path.name))
        sums.write_text("\n".join(lines) + "\n")
        return manifest_path, [archive, manifest_path], sums

    def test_a_complete_candidate_passes(self):
        every = [c.id for c in resolve(CATALOG, profile="release")]
        manifest_path, assets, sums = self.candidate(cells=every)
        release.check_inventory(
            manifest_path,
            profile="release",
            tag=TAG,
            sha="c" * 40,
            assets=assets,
            sums_path=sums,
        )

    def test_a_missing_cell_cannot_publish(self):
        every = [c.id for c in resolve(CATALOG, profile="release")][:-1]
        manifest_path, assets, sums = self.candidate(cells=every)
        with self.assertRaises(CIError) as caught:
            release.check_inventory(manifest_path, profile="release", tag=TAG, sha=None, assets=assets, sums_path=sums)
        self.assertIn("missing 1 declared cell", str(caught.exception))

    def test_a_desktop_only_candidate_cannot_claim_the_release_profile(self):
        desktop = [c.id for c in resolve(CATALOG, profile="desktop")]
        manifest_path, assets, sums = self.candidate(cells=desktop)
        with self.assertRaises(CIError):
            release.check_inventory(manifest_path, profile="release", tag=TAG, sha=None, assets=assets, sums_path=sums)

    def test_an_unqualified_platform_cannot_publish(self):
        every = [c.id for c in resolve(CATALOG, profile="release")]
        manifest_path, assets, sums = self.candidate(cells=every, unqualified=["ios"])
        with self.assertRaises(CIError) as caught:
            release.check_inventory(manifest_path, profile="release", tag=TAG, sha=None, assets=assets, sums_path=sums)
        self.assertIn("unqualified", str(caught.exception))

    def test_a_tag_that_names_another_version_cannot_publish(self):
        every = [c.id for c in resolve(CATALOG, profile="release")]
        manifest_path, assets, sums = self.candidate(cells=every)
        with self.assertRaises(CIError) as caught:
            release.check_inventory(
                manifest_path, profile="release", tag="v9.9", sha=None, assets=assets, sums_path=sums
            )
        self.assertIn("does not name version", str(caught.exception))

    def test_a_hash_mismatch_cannot_publish(self):
        every = [c.id for c in resolve(CATALOG, profile="release")]
        manifest_path, assets, sums = self.candidate(cells=every, sums_correct=False)
        with self.assertRaises(CIError) as caught:
            release.check_inventory(manifest_path, profile="release", tag=TAG, sha=None, assets=assets, sums_path=sums)
        self.assertIn("hashes", str(caught.exception))

    def test_a_candidate_from_another_revision_cannot_publish(self):
        every = [c.id for c in resolve(CATALOG, profile="release")]
        manifest_path, assets, sums = self.candidate(cells=every)
        with self.assertRaises(CIError) as caught:
            release.check_inventory(
                manifest_path, profile="release", tag=TAG, sha="d" * 40, assets=assets, sums_path=sums
            )
        self.assertIn("was built at", str(caught.exception))


class Lanes(unittest.TestCase):
    def test_a_documentation_change_reaches_only_the_docs_lane(self):
        import changed

        lanes = changed.lanes_for(["docs/index.rst"])
        self.assertTrue(lanes["docs"])
        self.assertFalse(lanes["hosted"])
        self.assertFalse(lanes["module"])

    def test_a_native_change_reaches_the_module_lane(self):
        import changed

        lanes = changed.lanes_for(["extension/godot/scene_tree.hpp"])
        self.assertTrue(lanes["module"])
        self.assertTrue(lanes["hosted"])
        self.assertTrue(lanes["native"])

    def test_a_pin_change_reaches_the_module_lane(self):
        import changed

        self.assertTrue(changed.lanes_for(["ci/engines.json"])["module"])


if __name__ == "__main__":
    unittest.main()
