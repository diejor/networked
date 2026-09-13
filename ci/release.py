#!/usr/bin/env python3
"""Decide whether a candidate may be published, and publish exactly it."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from common import ROOT, addon_version, digest_of, fail, load_json, main, release_stem, write_json
from matrix import load_catalog, resolve


def tag_version(tag: str) -> str:
    return tag[1:] if tag.startswith("v") else tag


def check_inventory(
    manifest_path: Path,
    *,
    profile: str,
    tag: str | None,
    sha: str | None,
    assets: list[Path],
    sums_path: Path,
) -> dict:
    manifest = load_json(manifest_path)
    catalog = load_catalog()
    declared = {cell.id for cell in resolve(catalog, profile=profile)}
    present = {cell["id"] for cell in manifest["cells"]}

    missing = sorted(declared - present)
    if missing:
        raise fail("the candidate is missing %d declared cell(s): %s" % (len(missing), ", ".join(missing)))
    extra = sorted(present - declared)
    if extra:
        raise fail("the candidate carries cells the profile does not declare: %s" % ", ".join(extra))

    unqualified = manifest.get("unqualified", [])
    if unqualified:
        raise fail(
            "the candidate names %s as unqualified, so it is not the product this profile promises"
            % ", ".join(unqualified)
        )

    version = addon_version()
    if manifest["version"] != version:
        raise fail("the manifest says version %s but the addon declares %s" % (manifest["version"], version))
    if tag and tag_version(tag) != version:
        raise fail("tag %s does not name version %s" % (tag, version))
    if sha and manifest["source"]["sha"] != sha:
        raise fail("the manifest was built at %s, not at %s" % (manifest["source"]["sha"][:12], sha[:12]))

    for asset in assets:
        if not asset.is_file():
            raise fail("declared asset %s is missing" % asset)

    recorded = {}
    for line in sums_path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            digest, name = line.split(None, 1)
            recorded[name.strip()] = digest
    for asset in assets:
        if asset.name not in recorded:
            raise fail("%s is not listed in %s" % (asset.name, sums_path.name))
        actual = digest_of(asset)
        if actual != recorded[asset.name]:
            raise fail("%s hashes %s, but %s records %s" % (asset.name, actual, sums_path.name, recorded[asset.name]))

    print("INVENTORY %d cells, %d assets, version %s" % (len(present), len(assets), version))
    return manifest


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", default="release")
    parser.add_argument("--dist", type=Path, default=ROOT / "dist")
    parser.add_argument("--tag", default="")
    parser.add_argument("--sha", default="")
    parser.add_argument("--publish", action="store_true", help="Without this the run is a rehearsal.")
    parser.add_argument("--record", type=Path)
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    version = addon_version()
    manifest_path = args.dist / ("%s.manifest.json" % release_stem(version))
    archive = args.dist / ("%s.zip" % release_stem(version))
    sums = args.dist / "SHA256SUMS.txt"
    assets = [archive, manifest_path]

    manifest = check_inventory(
        manifest_path,
        profile=args.profile,
        tag=args.tag or None,
        sha=args.sha or None,
        assets=assets,
        sums_path=sums,
    )

    inventory = {
        "version": version,
        "tag": args.tag,
        "source": manifest["source"],
        "assets": [{"name": path.name, "sha256": digest_of(path), "bytes": path.stat().st_size} for path in assets]
        + [{"name": sums.name, "sha256": digest_of(sums), "bytes": sums.stat().st_size}],
        "cells": [cell["id"] for cell in manifest["cells"]],
        "published": bool(args.publish),
    }
    if args.record:
        write_json(args.record, inventory)
    print(json.dumps(inventory, indent=2))

    if not args.publish:
        print("REHEARSAL the inventory gate passed and nothing was published")
        return 0
    if not args.tag:
        raise fail("publication needs a tag")
    print("PUBLISHABLE %s" % args.tag)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(lambda: run_cli()))
