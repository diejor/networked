"""Fixtures for the ways a multi-example deploy goes out wrong but green."""

from __future__ import annotations

import json
import os
import shutil
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import CIError  # noqa: E402
from export import (  # noqa: E402
    inject_turn_credentials,
    landing_page,
    load_examples,
    site_revision,
    stage_cloudflare,
)


def catalog_of(*entries: dict) -> dict:
    return {"schema": 1, "site": {"title": "T", "tagline": "L"}, "examples": list(entries)}


def entry(slug: str = "bomber", **overrides: str) -> dict:
    row = {"slug": slug, "title": "Bomber", "scene": "uid://abc", "blurb": "b"}
    row.update(overrides)
    return row


class Catalog(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir, ignore_errors=True)

    def write(self, payload: dict) -> Path:
        path = self.dir / "examples.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def test_the_shipped_catalog_loads(self):
        catalog = load_examples()
        self.assertTrue(catalog["examples"])

    def test_an_empty_catalog_is_refused(self):
        with self.assertRaises(CIError):
            load_examples(self.write(catalog_of()))

    def test_an_entry_missing_its_scene_is_refused(self):
        with self.assertRaises(CIError):
            load_examples(self.write(catalog_of(entry(scene=""))))

    def test_a_slug_that_is_not_url_safe_is_refused(self):
        for slug in ("Rocket League", "rocket_league", "../etc", "rocket-"):
            with self.subTest(slug=slug), self.assertRaises(CIError):
                load_examples(self.write(catalog_of(entry(slug))))

    def test_a_slug_published_twice_is_refused(self):
        with self.assertRaises(CIError):
            load_examples(self.write(catalog_of(entry("bomber"), entry("bomber"))))

    def test_a_scene_named_by_path_is_refused(self):
        with self.assertRaises(CIError):
            load_examples(self.write(catalog_of(entry(scene="res://examples/bomber/main.tscn"))))

    def test_the_landing_page_links_every_slug_and_escapes_its_prose(self):
        page = landing_page(catalog_of(entry("bomber", title="A & B"), entry("racing")))
        self.assertIn('href="bomber/"', page)
        self.assertIn('href="racing/"', page)
        self.assertIn("A &amp; B", page)

    def test_the_footer_credits_the_revision_the_deployment_resolved(self):
        page = landing_page(catalog_of(entry()), "0123456789abcdef")
        self.assertIn("<code>0123456789ab</code>", page)

    def test_a_tree_with_no_git_still_renders_a_page(self):
        cwd = Path.cwd()
        os.chdir(self.dir)
        self.addCleanup(os.chdir, cwd)
        with unittest.mock.patch("export.git_head", side_effect=OSError("not a git repository")):
            self.assertEqual(site_revision(), "unknown")
            self.assertIn("<code>unknown</code>", landing_page(catalog_of(entry())))


class CloudflareStaging(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir, ignore_errors=True)

    def plant(self, relative: str, size: int) -> Path:
        path = self.dir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"\x00" * size)
        return path

    def test_a_file_in_a_subpath_is_compressed_and_routed_from_the_root(self):
        self.plant("bomber/index.wasm", 4096)
        self.plant("racing/index.wasm", 4096)
        report = stage_cloudflare(self.dir, limit=1024)

        self.assertEqual(sorted(report["compressed"]), ["bomber/index.wasm", "racing/index.wasm"])
        self.assertTrue((self.dir / "bomber" / "index.wasm.br").is_file())
        self.assertFalse((self.dir / "bomber" / "index.wasm").exists())

        redirects = (self.dir / "_redirects").read_text(encoding="utf-8")
        self.assertIn("/bomber/index.wasm /bomber/index.wasm.br 200", redirects)
        self.assertIn("/racing/index.wasm /racing/index.wasm.br 200", redirects)

    def test_the_headers_name_the_subpath_and_keep_the_isolation_rule(self):
        self.plant("bomber/index.wasm", 4096)
        stage_cloudflare(self.dir, limit=1024)
        headers = (self.dir / "_headers").read_text(encoding="utf-8")

        self.assertIn("/*\n  Cross-Origin-Opener-Policy: same-origin", headers)
        self.assertIn("Cross-Origin-Embedder-Policy: require-corp", headers)
        self.assertIn("/bomber/index.wasm\n  Content-Encoding: br", headers)
        self.assertIn("Content-Type: application/wasm", headers)

    def test_a_file_under_the_limit_is_left_alone(self):
        self.plant("bomber/index.pck", 64)
        report = stage_cloudflare(self.dir, limit=1024)

        self.assertEqual(report["compressed"], [])
        self.assertTrue((self.dir / "bomber" / "index.pck").is_file())
        self.assertEqual((self.dir / "_redirects").read_text(encoding="utf-8").strip(), "")

    def test_a_pck_is_served_as_an_opaque_stream_rather_than_wasm(self):
        self.plant("bomber/index.pck", 4096)
        stage_cloudflare(self.dir, limit=1024)
        headers = (self.dir / "_headers").read_text(encoding="utf-8")

        self.assertIn("Content-Type: application/octet-stream", headers)
        self.assertNotIn("application/wasm", headers)


class TurnCredentials(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir, ignore_errors=True)

    def project_of(self, body: str) -> Path:
        path = self.dir / "project.godot"
        path.write_text(body, encoding="utf-8")
        return path

    def test_the_header_lands_under_the_url_it_authenticates(self):
        path = self.project_of('[networked]\n\nwebrtc/turn_credentials_url="https://x/"\ninstall_as_default=true\n')
        inject_turn_credentials("s3cret", path)
        lines = path.read_text(encoding="utf-8").splitlines()

        self.assertEqual(lines[3], 'webrtc/turn_credentials_headers=PackedStringArray("X-Turn-Auth-Token: s3cret")')
        self.assertEqual(lines[4], "install_as_default=true")

    def test_an_existing_header_is_replaced_rather_than_doubled(self):
        path = self.project_of(
            '[networked]\n\nwebrtc/turn_credentials_url="https://x/"\n'
            'webrtc/turn_credentials_headers=PackedStringArray("X-Turn-Auth-Token: old")\n'
        )
        inject_turn_credentials("new", path)
        text = path.read_text(encoding="utf-8")

        self.assertEqual(text.count("turn_credentials_headers"), 1)
        self.assertIn("X-Turn-Auth-Token: new", text)
        self.assertNotIn("old", text)

    def test_a_project_with_no_url_to_anchor_to_is_refused(self):
        path = self.project_of("[networked]\n\ninstall_as_default=true\n")
        with self.assertRaises(CIError):
            inject_turn_credentials("s3cret", path)


if __name__ == "__main__":
    unittest.main()
