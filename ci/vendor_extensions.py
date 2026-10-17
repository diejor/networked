"""Place pinned desktop SDK extensions in a release."""

from __future__ import annotations

import re
import shutil
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

from common import CI_DIR, fail, load_json, sha256_file

DESKTOP = {"linux.x86_64", "windows.x86_64", "macos.universal"}


def desktop_key(key: str) -> bool:
    features = set(key.split("."))
    return (
        any(set(cell.split(".")) <= features for cell in DESKTOP)
        or "macos" in features
    )


def trim_manifest(text: str, destination: str) -> tuple[str, set[str]]:
    section = ""
    lines: list[str] = []
    files: set[str] = set()
    for line in text.splitlines():
        if line.startswith("["):
            section = line.strip("[]")
        if section in ("libraries", "dependencies") and "=" in line:
            key = line.split("=", 1)[0].strip()
            if not desktop_key(key):
                continue
            paths = re.findall(
                r'"([^"\n]+\.(?:so|dll|dylib|framework))"', line
            )
            for path in paths:
                relative = path.removeprefix("res://" + destination + "/")
                files.add(relative)
        lines.append(line)
    if not files:
        raise fail("the vendor manifest declares no desktop libraries")
    return "\n".join(lines) + "\n", files


def place(stage_root: Path, cache: Path) -> dict:
    pins = load_json(CI_DIR / "vendor_extensions.json")
    stage_root.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    records: dict = {}
    for name, pin in pins.items():
        archive = cache / (pin["sha256"] + ".zip")
        if not archive.exists():
            partial = archive.with_suffix(".partial")
            try:
                response = urllib.request.urlopen(pin["url"], timeout=120)
                with response:
                    with partial.open("wb") as output:
                        shutil.copyfileobj(response, output)
                partial.replace(archive)
            finally:
                partial.unlink(missing_ok=True)
        if sha256_file(archive) != pin["sha256"]:
            raise fail(
                "%s failed its SHA-256 check. Remove %s and retry."
                % (name, archive)
            )
        destination = stage_root / pin["destination"]
        destination.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive) as bundle:
            prefix = pin["prefix"]
            source_manifest = pin.get("archive_manifest", pin["manifest"])
            text = bundle.read(prefix + source_manifest).decode("utf-8")
            text = text.replace(
                "res://" + prefix, "res://" + pin["destination"] + "/"
            )
            manifest, files = trim_manifest(text, pin["destination"])
            for entry in bundle.infolist():
                if entry.is_dir() or not entry.filename.startswith(prefix):
                    continue
                relative = entry.filename.removeprefix(prefix)
                parts = PurePosixPath(relative).parts
                if ".." in parts or PurePosixPath(relative).is_absolute():
                    raise fail("%s holds an unsafe path" % name)
                licensed = len(parts) == 1 and "license" in relative.lower()
                library = relative in files or any(
                    relative.startswith(f + "/") for f in files
                )
                if not licensed and not library:
                    continue
                target = destination / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(bundle.read(entry))
            (destination / pin["manifest"]).write_text(
                manifest, encoding="utf-8"
            )
            for relative in files:
                if not (destination / relative).exists():
                    raise fail("%s is missing %s" % (name, relative))
        (destination / "SOURCES.txt").write_text(
            pin["source"] + "\n", encoding="utf-8"
        )
        records[name] = {
            key: pin[key] for key in ("version", "url", "sha256", "source")
        }
    return records
