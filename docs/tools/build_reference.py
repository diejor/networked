#!/usr/bin/env python3
"""Generate docs/classes/ from the addon's GDScript and its native registry."""

from __future__ import annotations

import argparse
import contextlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

DOCS = Path(__file__).resolve().parent.parent
ROOT = DOCS.parent
CI = ROOT / "ci"
DOC_CLASSES = ROOT / "extension" / "doc_classes"

XML_FLOOR = 500


def run(argv: list[str], *, cwd: Path, check: bool = True) -> int:
    print("+ %s" % " ".join(argv), flush=True)
    completed = subprocess.run(argv, cwd=str(cwd), check=False)
    if check and completed.returncode != 0:
        raise SystemExit("exit %d from %s" % (completed.returncode, argv[0]))
    return completed.returncode


@contextlib.contextmanager
def ignored_inputs_parked():
    parked = []
    try:
        for path in ROOT.iterdir():
            if path == DOCS or path.name.startswith("."):
                continue
            if not path.is_dir() or not (path / ".gdignore").is_file():
                continue
            hidden = path.with_name(".doctool-" + path.name)
            if hidden.exists():
                raise SystemExit("Restore %s before generating docs." % hidden)
            path.rename(hidden)
            parked.append((path, hidden))
        yield
    finally:
        for path, hidden in reversed(parked):
            if path.exists():
                shutil.copytree(path, hidden, dirs_exist_ok=True)
                shutil.rmtree(path)
            hidden.rename(path)


def generate(godot: str, *, output: Path, keep_going: bool) -> None:
    api = DOCS / "api_new"
    filtered = DOCS / "api_filtered_new"
    classes = DOCS / "classes_new"
    for path in (api, filtered, classes):
        if path.exists():
            shutil.rmtree(path)
    api.mkdir(parents=True)

    run([sys.executable, "tools/build_make_rst.py", "--check"], cwd=DOCS, check=not keep_going)

    run([godot, "--headless", "--path", str(ROOT), "--import"], cwd=ROOT, check=False)

    with ignored_inputs_parked():
        run(
            [
                godot, "--doctool", str(api), "--gdscript-docs", ".",
                "--headless", "--quit", "--log-file",
                str(ROOT / ".godot" / "doctool.log"),
            ],
            cwd=ROOT,
            check=True,
        )

    written = sorted(api.glob("*.xml"))
    if len(written) < XML_FLOOR:
        raise SystemExit("doctool wrote %d xml files, expected at least %d" % (len(written), XML_FLOOR))
    print("doctool wrote %d xml files" % len(written))

    native = sorted(DOC_CLASSES.glob("*.xml"))
    if not native:
        raise SystemExit("extension/doc_classes holds no XML, so the native reference would be missing")
    for path in native:
        shutil.copy2(path, api / path.name)
    print("copied %d native class descriptions" % len(native))

    run([sys.executable, "tools/filter_private.py", str(api), str(filtered)], cwd=DOCS)
    run([sys.executable, "tools/make_rst.py", str(filtered), "--output", str(classes)], cwd=DOCS)

    produced = sorted(classes.glob("*.rst"))
    if not produced:
        raise SystemExit("make_rst.py produced no RST")

    for name, staged in (("api", api), ("api_filtered", filtered), (output.name, classes)):
        final = DOCS / name
        if final.exists():
            shutil.rmtree(final)
        staged.rename(final)
    print("REFERENCE %d pages under %s" % (len(produced), output))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", default=os.environ.get("GODOT", ""))
    parser.add_argument("--engine", default="normal")
    parser.add_argument("--output", type=Path, default=DOCS / "classes")
    parser.add_argument("--keep-going", action="store_true")
    args = parser.parse_args()

    godot = args.godot
    if not godot:
        sys.path.insert(0, str(CI))
        from engine import install

        godot = str(install(args.engine, slot="linux.x86_64", dest=ROOT / ".engines", timeout=900, record_sha=False))
    generate(godot, output=args.output, keep_going=args.keep_going)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
