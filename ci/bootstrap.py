#!/usr/bin/env python3
"""Install the pinned lint and rehearsal binaries, and record the host."""

from __future__ import annotations

import argparse
import json
import os
import platform as host_platform
import shutil
import stat
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

from common import CI_DIR, ROOT, fail, load_json, main, read_pins, run, sha256_file, write_json

TOOLS_FILE = CI_DIR / "tools.json"
TOOLS_DIR = CI_DIR / ".tools"

HOST_PROBES = {
    "scons": ["scons", "--version"],
    "godot": ["godot", "--version"],
    "python": ["python3", "--version"],
    "emcc": ["emcc", "--version"],
    "adb": ["adb", "--version"],
    "docker": ["docker", "--version"],
    "mingw": ["x86_64-w64-mingw32-g++", "--version"],
    "clang": ["clang", "--version"],
    "lipo": ["lipo", "-info"],
    "xcodebuild": ["xcodebuild", "-version"],
    "chromium": ["chromium", "--version"],
    "readelf": ["readelf", "--version"],
}


def probe(argv: list[str]) -> str:
    if not shutil.which(argv[0]):
        return ""
    completed = run(argv, capture=True, check=False, timeout=60)
    text = (completed.stdout or completed.stderr or "").strip()
    return text.splitlines()[0] if text else ""


def host_record() -> dict:
    usage = shutil.disk_usage(ROOT)
    return {
        "platform": host_platform.platform(),
        "machine": host_platform.machine(),
        "cpus": os.cpu_count(),
        "disk_free_gib": round(usage.free / (1 << 30), 1),
        "tools": {name: probe(argv) for name, argv in sorted(HOST_PROBES.items())},
        "android_ndk_root": os.environ.get("ANDROID_NDK_ROOT", ""),
    }


def install_tool(name: str, record: dict, *, dest: Path, record_sha: bool, catalog: dict) -> Path:
    version = record["version"]
    url = record["url"].format(version=version)
    member = record["member"].format(version=version)
    dest.mkdir(parents=True, exist_ok=True)
    target = dest / name

    with tempfile.TemporaryDirectory() as tmp:
        archive = Path(tmp) / Path(url).name
        print("+ download %s" % url)
        try:
            with urllib.request.urlopen(url, timeout=600) as response:
                archive.write_bytes(response.read())
        except OSError as error:
            raise fail("could not download %s: %s" % (url, error))
        digest = sha256_file(archive)
        if record_sha:
            record["sha256"] = digest
            write_json(TOOLS_FILE, catalog)
            print("recorded sha256 for %s: %s" % (name, digest))
        elif record["sha256"] and record["sha256"] != digest:
            raise fail("%s hashes %s, but the catalog pins %s" % (name, digest, record["sha256"]))
        elif not record["sha256"]:
            print("::warning::no checksum pinned for %s (sha256 %s)" % (name, digest))

        with tarfile.open(archive) as bundle:
            try:
                extracted = bundle.extractfile(member)
            except KeyError:
                extracted = None
            if extracted is None:
                raise fail("%s holds no member '%s'" % (archive.name, member))
            target.write_bytes(extracted.read())
    target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    print("TOOL %s %s at %s" % (name, version, target))
    return target


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)

    tools = sub.add_parser("tools")
    tools.add_argument("names", nargs="*")
    tools.add_argument("--dest", type=Path, default=TOOLS_DIR)
    tools.add_argument("--record-sha256", action="store_true")

    host = sub.add_parser("host")
    host.add_argument("--record", type=Path)

    sub.add_parser("scons")
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.action == "scons":
        version = read_pins().get("SCONS_VERSION")
        if not version:
            raise fail("extension/deps.env declares no SCONS_VERSION")
        run([sys.executable, "-m", "pip", "install", "scons==%s" % version])
        return 0
    if args.action == "host":
        record = host_record()
        print(json.dumps(record, indent=2))
        if args.record:
            write_json(args.record, record)
        return 0
    catalog = load_json(TOOLS_FILE)
    wanted = args.names or sorted(catalog["tools"])
    for name in wanted:
        if name not in catalog["tools"]:
            raise fail("unknown tool '%s'; known: %s" % (name, ", ".join(sorted(catalog["tools"]))))
        install_tool(
            name,
            catalog["tools"][name],
            dest=args.dest,
            record_sha=args.record_sha256,
            catalog=catalog,
        )
    print(str(args.dest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(lambda: run_cli()))
