"""Shared helpers for the public CI scripts."""

from __future__ import annotations

import hashlib
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path

CI_DIR = Path(__file__).resolve().parent
ROOT = CI_DIR.parent
DEPS_FILE = ROOT / "extension" / "deps.env"


class CIError(Exception):
    """A refusal with a message meant for a build log."""


def fail(message: str) -> "CIError":
    return CIError(message)


def read_pins(path: Path | None = None) -> dict[str, str]:
    pins: dict[str, str] = {}
    for raw in (path or DEPS_FILE).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise fail("deps.env line is not key=value: %r" % raw)
        key, value = line.split("=", 1)
        pins[key.strip()] = value.strip()
    return pins


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise fail("missing %s" % path)
    except json.JSONDecodeError as error:
        raise fail("%s is not valid JSON: %s" % (path, error))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_tree(path: Path) -> str:
    """A digest over a directory, so a framework hashes like a file."""
    digest = hashlib.sha256()
    for entry in sorted(p for p in path.rglob("*") if p.is_file()):
        digest.update(str(entry.relative_to(path)).encode("utf-8"))
        digest.update(sha256_file(entry).encode("ascii"))
    return digest.hexdigest()


def digest_of(path: Path) -> str:
    return sha256_tree(path) if path.is_dir() else sha256_file(path)


def run(
    argv: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    timeout: float | None = None,
    check: bool = True,
    capture: bool = False,
) -> subprocess.CompletedProcess:
    """Run an argument array under a deadline."""
    printable = " ".join(shlex.quote(part) for part in argv)
    print("+ %s" % printable, flush=True)
    merged = dict(os.environ)
    merged.update(env or {})
    try:
        completed = subprocess.run(
            argv,
            cwd=str(cwd or ROOT),
            env=merged,
            timeout=timeout,
            check=False,
            text=True,
            capture_output=capture,
        )
    except FileNotFoundError:
        raise fail("%s is not on PATH" % argv[0])
    except subprocess.TimeoutExpired:
        raise fail("timed out after %ss: %s" % (timeout, printable))
    if check and completed.returncode != 0:
        if capture:
            sys.stdout.write(completed.stdout or "")
            sys.stderr.write(completed.stderr or "")
        raise fail("exit %d from %s" % (completed.returncode, printable))
    return completed


def git_head(root: Path | None = None) -> str:
    completed = run(
        ["git", "rev-parse", "HEAD"],
        cwd=root or ROOT,
        capture=True,
        timeout=60,
    )
    return completed.stdout.strip()


def git_dirty(root: Path | None = None) -> bool:
    completed = run(
        ["git", "status", "--porcelain"],
        cwd=root or ROOT,
        capture=True,
        timeout=120,
    )
    return bool(completed.stdout.strip())


def addon_version() -> str:
    text = (ROOT / "addons" / "networked" / "plugin.cfg").read_text(encoding="utf-8")
    for line in text.splitlines():
        if line.startswith("version="):
            return line.split("=", 1)[1].strip().strip('"')
    raise fail("addons/networked/plugin.cfg declares no version")


def compatibility_minimum() -> str:
    text = (ROOT / "extension" / "networked.gdextension").read_text(encoding="utf-8")
    for line in text.splitlines():
        if line.startswith("compatibility_minimum"):
            return line.split("=", 1)[1].strip().strip('"')
    raise fail("the extension manifest declares no compatibility_minimum")


def release_stem(version: str | None = None) -> str:
    """The name a release asset carries, engine version included."""
    return "networked-v%s-godot%s" % (version or addon_version(), compatibility_minimum())


def gdignore(directory: Path) -> None:
    """Keep a generated directory out of the engine's resource scan."""
    directory.mkdir(parents=True, exist_ok=True)
    marker = directory / ".gdignore"
    if not marker.exists():
        marker.write_text("", encoding="utf-8")


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def github_output(**values: str) -> None:
    """Publish step outputs when running inside a GitHub job."""
    destination = os.environ.get("GITHUB_OUTPUT")
    if not destination:
        return
    with open(destination, "a", encoding="utf-8") as handle:
        for key, value in values.items():
            handle.write("%s=%s\n" % (key, value))


def main(entry) -> int:
    """Turn a CIError into an annotated non-zero exit rather than a traceback."""
    try:
        return entry() or 0
    except CIError as error:
        print("::error::%s" % error, file=sys.stderr)
        return 1
