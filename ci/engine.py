#!/usr/bin/env python3
"""Install and qualify a Godot editor from the engine catalog."""

from __future__ import annotations

import argparse
import os
import shutil
import stat
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

from common import CI_DIR, ROOT, fail, load_json, main, run, sha256_file, write_json

ENGINES_FILE = CI_DIR / "engines.json"

CAPABILITY_PROBES = {
    "space_step": 'PhysicsServer3D.has_method("space_step")',
}

PROBE_PROJECT = """[application]

config/name="netw-engine-probe"
config/features=PackedStringArray("4.4")
"""

PROBE_SCRIPT = """extends SceneTree


func _init() -> void:
%s
\tquit(0)
"""


def load_catalog(path: Path | None = None) -> dict:
    catalog = load_json(path or ENGINES_FILE)
    if catalog.get("schema") != 1:
        raise fail("engines.json declares an unknown schema")
    return catalog


def engine(catalog: dict, name: str | None) -> tuple[str, dict]:
    chosen = name or catalog["default"]
    if chosen not in catalog["engines"]:
        raise fail("unknown engine '%s'; known: %s" % (chosen, ", ".join(sorted(catalog["engines"]))))
    return chosen, catalog["engines"][chosen]


def asset_url(record: dict, slot: str) -> tuple[str, str]:
    release = record["release"]
    assets = release["assets"]
    if slot not in assets:
        raise fail("engine has no '%s' asset; known: %s" % (slot, ", ".join(sorted(assets))))
    filename = assets[slot]
    url = "https://github.com/%s/%s/releases/download/%s/%s" % (
        release["owner"],
        release["repo"],
        release["tag"],
        filename,
    )
    return url, filename


def entrypoint_name(record: dict, slot: str) -> str:
    """The executable inside the archive, which is not always its name."""
    url, filename = asset_url(record, slot)
    del url
    default = filename.removesuffix(".zip")
    return record["release"].get("entrypoints", {}).get(slot, default)


def download(url: str, destination: Path) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    print("+ download %s" % url)
    try:
        with urllib.request.urlopen(url, timeout=600) as response:
            destination.write_bytes(response.read())
    except OSError as error:
        raise fail("could not download %s: %s" % (url, error))
    return destination


def normalized_version(reported: str) -> str:
    """`4.7.2.stable.official.ed1daf0` and `4.7.2-stable` compared as one shape."""
    parts = reported.strip().replace("-", ".").split(".")
    keep = []
    for part in parts:
        keep.append(part)
        if not part.isdigit():
            break
    return ".".join(keep)


def probe_capabilities(binary: Path, expected: dict[str, bool], *, timeout: float) -> dict[str, bool]:
    lines = []
    for name in sorted(expected):
        if name not in CAPABILITY_PROBES:
            raise fail("no probe is written for capability '%s'" % name)
        lines.append('\tprint("CAP %s=%%s" %% (%s))' % (name, CAPABILITY_PROBES[name]))
    if not lines:
        return {}
    with tempfile.TemporaryDirectory() as tmp:
        project = Path(tmp)
        (project / "project.godot").write_text(PROBE_PROJECT, encoding="utf-8")
        script = project / "probe.gd"
        script.write_text(PROBE_SCRIPT % "\n".join(lines), encoding="utf-8")
        completed = run(
            [str(binary), "--headless", "--path", str(project), "--script", str(script)],
            capture=True,
            check=False,
            timeout=timeout,
        )
    text = (completed.stdout or "") + (completed.stderr or "")
    found: dict[str, bool] = {}
    for line in text.splitlines():
        if line.startswith("CAP "):
            key, _, value = line[4:].partition("=")
            found[key.strip()] = value.strip().lower() == "true"
    missing = [name for name in expected if name not in found]
    if missing:
        sys.stdout.write(text)
        raise fail("the capability probe returned no value for %s" % ", ".join(missing))
    return found


def install(
    name: str | None,
    *,
    slot: str,
    dest: Path,
    timeout: float,
    record_sha: bool,
    catalog_path: Path | None = None,
) -> Path:
    catalog = load_catalog(catalog_path)
    chosen, record = engine(catalog, name)
    url, filename = asset_url(record, slot)

    dest.mkdir(parents=True, exist_ok=True)
    archive = dest / filename
    if not archive.is_file():
        download(url, archive)
    digest = sha256_file(archive)
    declared = record["release"].get("sha256", {}).get(slot)
    if record_sha:
        record["release"].setdefault("sha256", {})[slot] = digest
        write_json(catalog_path or ENGINES_FILE, catalog)
        print("recorded sha256 for %s/%s: %s" % (chosen, slot, digest))
    elif declared and declared != digest:
        raise fail("%s/%s hashes %s, but the catalog pins %s" % (chosen, slot, digest, declared))
    elif not declared:
        print("::warning::the catalog pins no checksum for %s/%s (sha256 %s)" % (chosen, slot, digest))

    entry = entrypoint_name(record, slot)
    with zipfile.ZipFile(archive) as bundle:
        names = bundle.namelist()
        if entry not in names:
            raise fail(
                "%s does not contain the expected entrypoint '%s'; it holds %s"
                % (archive.name, entry, ", ".join(names[:8]))
            )
        bundle.extractall(dest)
    unpacked = dest / entry
    binary = dest / "godot"
    if binary.exists():
        binary.unlink()
    unpacked.replace(binary)
    binary.chmod(binary.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    reported = run([str(binary), "--version"], capture=True, timeout=timeout).stdout
    if normalized_version(reported) != normalized_version(record["version"]):
        raise fail("%s reports %s, but the catalog declares %s" % (binary, reported.strip(), record["version"]))

    expected = record.get("capabilities", {})
    found = probe_capabilities(binary, expected, timeout=timeout)
    for capability, want in sorted(expected.items()):
        got = found[capability]
        print("CAPABILITY %s %s=%s (declared %s)" % (chosen, capability, got, want))
        if got != want:
            raise fail("engine '%s' declares %s=%s but the binary reports %s" % (chosen, capability, want, got))
    print("ENGINE %s at %s" % (chosen, binary))
    return binary


def install_templates(
    name: str | None,
    *,
    dest: Path,
    timeout: float,
    record_sha: bool,
    catalog_path: Path | None = None,
) -> Path:
    """Unpack the export templates the same engine published."""
    del timeout
    catalog = load_catalog(catalog_path)
    chosen, record = engine(catalog, name)
    url, filename = asset_url(record, "export_templates")

    cache = dest.parent / "downloads"
    archive = cache / filename
    if not archive.is_file():
        download(url, archive)
    digest = sha256_file(archive)
    declared = record["release"].get("sha256", {}).get("export_templates")
    if record_sha:
        record["release"].setdefault("sha256", {})["export_templates"] = digest
        write_json(catalog_path or ENGINES_FILE, catalog)
        print("recorded sha256 for %s/export_templates: %s" % (chosen, digest))
    elif declared and declared != digest:
        raise fail("%s export templates hash %s, but the catalog pins %s" % (chosen, digest, declared))

    target = dest / record["templates_dir"]
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    with zipfile.ZipFile(archive) as bundle:
        members = [name for name in bundle.namelist() if name.startswith("templates/") and not name.endswith("/")]
        if not members:
            raise fail("%s holds no templates/ directory" % archive.name)
        for member in members:
            data = bundle.read(member)
            (target / Path(member).name).write_bytes(data)
    stamp = target / "version.txt"
    if not stamp.is_file():
        raise fail("the unpacked templates carry no version.txt")
    reported = stamp.read_text(encoding="utf-8").strip()
    if reported != record["templates_dir"]:
        raise fail("the templates report %s, but the catalog names %s" % (reported, record["templates_dir"]))
    print("TEMPLATES %s at %s (%d files)" % (chosen, target, len(members)))
    return target


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine")
    parser.add_argument("--catalog", type=Path, default=ENGINES_FILE)
    parser.add_argument("--timeout", type=float, default=900.0)
    sub = parser.add_subparsers(dest="action", required=True)

    setup = sub.add_parser("install")
    setup.add_argument("--slot", default="linux.x86_64")
    setup.add_argument("--dest", type=Path, default=ROOT / ".engines")
    setup.add_argument("--record-sha256", action="store_true")

    templates = sub.add_parser("templates")
    templates.add_argument("--dest", type=Path, default=ROOT / ".engines" / "export_templates")
    templates.add_argument("--record-sha256", action="store_true")

    field = sub.add_parser("source")
    field.add_argument("field", choices=("repo", "branch", "ref", "version", "templates_dir"))

    check = sub.add_parser("probe")
    check.add_argument("--binary", type=Path, required=True)
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.action == "install":
        binary = install(
            args.engine,
            slot=args.slot,
            dest=args.dest,
            timeout=args.timeout,
            record_sha=args.record_sha256,
            catalog_path=args.catalog,
        )
        from common import github_output

        github_output(binary=str(binary))
        return 0
    if args.action == "templates":
        target = install_templates(
            args.engine,
            dest=args.dest,
            timeout=args.timeout,
            record_sha=args.record_sha256,
            catalog_path=args.catalog,
        )
        from common import github_output

        github_output(templates=str(target))
        return 0
    catalog = load_catalog(args.catalog)
    chosen, record = engine(catalog, args.engine)
    if args.action == "source":
        if args.field in ("version", "templates_dir"):
            print(record[args.field])
        else:
            print(record["source"][args.field])
        return 0
    expected = record.get("capabilities", {})
    found = probe_capabilities(args.binary, expected, timeout=args.timeout)
    for capability, want in sorted(expected.items()):
        print("CAPABILITY %s %s=%s (declared %s)" % (chosen, capability, found[capability], want))
        if found[capability] != want:
            raise fail(
                "engine '%s' declares %s=%s but %s reports %s"
                % (chosen, capability, want, args.binary, found[capability])
            )
    return 0


if __name__ == "__main__":
    os.environ.setdefault("GODOT_SILENCE_ROOT_WARNING", "1")
    raise SystemExit(main(lambda: run_cli()))
