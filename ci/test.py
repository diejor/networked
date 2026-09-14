#!/usr/bin/env python3
"""Bounded test lanes, each producing a report something else can grade."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path

from common import ROOT, fail, main, run

REPORTS = ROOT / "reports"
TMP = ROOT / "tmp"

PROBE_CLASSES = ("Netw", "NetwEntity", "NetwMultiplayer")

TEARDOWN_SIGNATURES = (
    "Program crashed with signal 11",
    "leaked instance",
    "ObjectDB instances leaked at exit",
)

LOAD_FAILURES = (
    "Failed to load script",
    "Can't open dynamic library",
    "GDExtension dynamic library not found",
    "Error loading extension",
    "Cannot find the entry symbol",
)

PARSE_SWEEP = """extends SceneTree

const ROOTS: Array[String] = [%s]


func _init() -> void:
\tvar files: Array[String] = []
\tfor root in ROOTS:
\t\t_collect(root, files)
\tfiles.sort()
\tfor path in files:
\t\tResourceLoader.load(path, "Script", ResourceLoader.CACHE_MODE_IGNORE)
\tprint("PARSE_SWEPT files=%%d" %% files.size())
\tquit(0)


func _collect(dir_path: String, out: Array[String]) -> void:
\tvar dir := DirAccess.open(dir_path)
\tif dir == null:
\t\treturn
\tdir.list_dir_begin()
\tvar entry := dir.get_next()
\twhile entry != "":
\t\tvar child := dir_path.path_join(entry)
\t\tif dir.current_is_dir():
\t\t\tif not entry.begins_with("."):
\t\t\t\t_collect(child, out)
\t\telif entry.ends_with(".gd"):
\t\t\tout.append(child)
\t\tentry = dir.get_next()
\tdir.list_dir_end()
"""

CLASS_PROBE = """extends SceneTree


func _init() -> void:
\tvar missing: Array[String] = []
\tfor name in [%s]:
\t\tif not ClassDB.class_exists(name):
\t\t\tmissing.append(name)
\tif missing.is_empty():
\t\tprint("CLASSES ok")
\t\tquit(0)
\t\treturn
\tprinterr("CLASSES missing %%s" %% ", ".join(missing))
\tquit(1)
"""


CONSUMER_PROJECT = """config_version=5

[application]

config/name="netw-clean-consumer"
config/features=PackedStringArray("4.4")
"""

CONSUMER_PROBE = """extends SceneTree


func _init() -> void:
\tvar wanted: Array[String] = [%s]
\tvar missing: Array[String] = []
\tfor name in wanted:
\t\tif not ClassDB.class_exists(name):
\t\t\tmissing.append(name)
\tvar made: Object = null
\tif missing.is_empty():
\t\tmade = ClassDB.instantiate(wanted[0])
\tprint("CONSUMER found=%%d missing=%%s instantiated=%%s"
\t\t%% [wanted.size() - missing.size(), missing, made != null])
\tquit(0 if missing.is_empty() and made != null else 1)
"""


def _write_script(name: str, text: str) -> Path:
    TMP.mkdir(parents=True, exist_ok=True)
    path = TMP / name
    path.write_text(text, encoding="utf-8")
    return path


def _lane_env(lane: str | None) -> dict[str, str]:
    """A runtime directory of this lane's own, so `user://` cannot be shared."""
    if lane is None:
        return {}
    home = TMP / "home" / lane
    home.mkdir(parents=True, exist_ok=True)
    return {
        "HOME": str(home),
        "XDG_DATA_HOME": str(home / "share"),
        "XDG_CONFIG_HOME": str(home / "config"),
        "XDG_CACHE_HOME": str(home / "cache"),
    }


def _godot(
    godot: str,
    argv: list[str],
    *,
    log: Path,
    timeout: float,
    lane: str | None = None,
    check: bool = True,
) -> int:
    log.parent.mkdir(parents=True, exist_ok=True)
    completed = run(
        [godot] + argv,
        timeout=timeout,
        check=False,
        capture=True,
        env=_lane_env(lane),
    )
    text = (completed.stdout or "") + (completed.stderr or "")
    log.write_text(text, encoding="utf-8")
    sys.stdout.write(text)
    if check and completed.returncode != 0:
        raise fail("godot exited %d; see %s" % (completed.returncode, log))
    return completed.returncode


def lane_import(godot: str, *, timeout: float) -> int:
    """Import the project and validate that its scripts load."""
    log = TMP / "import.log"
    code = _godot(godot, ["--headless", "--path", str(ROOT), "--import"], log=log, timeout=timeout, check=False)
    text = log.read_text(encoding="utf-8", errors="replace")
    failures = [line for line in text.splitlines() if any(marker in line for marker in LOAD_FAILURES)]
    if failures:
        for line in failures[:20]:
            print("ERROR %s" % line.strip())
        raise fail("the import could not load the project's own code")
    if code != 0:
        if not any(signature in text for signature in TEARDOWN_SIGNATURES):
            raise fail("import exited %d and the log names no known teardown fault" % code)
        print("::warning::import exited %d after a known teardown fault; " "the outcome checks below decide" % code)
    if not (ROOT / ".godot").is_dir():
        raise fail("the import produced no .godot directory")
    return lane_classes(godot, timeout=timeout)


def lane_classes(godot: str, *, timeout: float) -> int:
    names = ", ".join('"%s"' % name for name in PROBE_CLASSES)
    script = _write_script("class_probe.gd", CLASS_PROBE % names)
    _godot(
        godot,
        ["--headless", "--path", str(ROOT), "--script", str(script)],
        log=TMP / "class_probe.log",
        timeout=timeout,
    )
    return 0


def lane_consumer(godot: str, *, archive: Path, workspace: Path, timeout: float) -> int:
    """Install the release zip into an empty project and load what it published."""
    if not archive.is_file():
        raise fail("no release archive at %s" % archive)
    if workspace.exists():
        shutil.rmtree(workspace)
    workspace.mkdir(parents=True)
    shutil.unpack_archive(str(archive), str(workspace))
    addon = workspace / "addons" / "networked"
    if not addon.is_dir():
        raise fail("%s unpacked no addons/networked" % archive.name)
    (workspace / "project.godot").write_text(CONSUMER_PROJECT, encoding="utf-8")

    names = ", ".join('"%s"' % name for name in PROBE_CLASSES)
    probe = workspace / "probe.gd"
    probe.write_text(CONSUMER_PROBE % names, encoding="utf-8")

    _godot(
        godot,
        ["--headless", "--path", str(workspace), "--import"],
        log=TMP / "consumer-import.log",
        timeout=timeout,
        lane="consumer",
        check=False,
    )
    _godot(
        godot,
        ["--headless", "--path", str(workspace), "--script", str(probe)],
        log=TMP / "consumer.log",
        timeout=timeout,
        lane="consumer",
    )
    return 0


def lane_parse(godot: str, roots: list[str], *, timeout: float) -> int:
    """Load every script under `roots` and report parse failures."""
    quoted = ", ".join('"%s"' % (root if root.startswith("res://") else "res://" + root.lstrip("/")) for root in roots)
    script = _write_script("parse_check.gd", PARSE_SWEEP % quoted)
    log = TMP / "parse_check.log"
    _godot(
        godot,
        ["--headless", "--path", str(ROOT), "--script", str(script)],
        log=log,
        timeout=timeout,
        check=False,
    )
    text = log.read_text(encoding="utf-8", errors="replace")
    if "PARSE_SWEPT" not in text:
        raise fail("the parse sweep did not finish; read %s" % log)
    failures = sorted(
        {
            line.split('"')[1]
            for line in text.splitlines()
            if "Failed to load script" in line and '"' in line and "thirdparty/godot/modules/gdscript/tests" not in line
        }
    )
    for path in failures:
        print("ERROR %s" % path)
    print("PARSE errors=%d" % len(failures))
    if failures:
        raise fail("%d script(s) did not parse" % len(failures))
    return 0


def lane_native(godot: str, *, timeout: float, native_filter: str | None) -> int:
    argv = [
        "--headless",
        "--fixed-fps",
        "60",
        "--path",
        str(ROOT),
        "--script",
        "res://tests/native/run_native_tests.gd",
    ]
    if native_filter:
        argv += ["--", "--native-filter=%s" % native_filter]
    _godot(godot, argv, log=TMP / "native.log", timeout=timeout, lane="native", check=False)
    report = REPORTS / "native" / "results.xml"
    if not report.is_file():
        raise fail("the hosted tier wrote no %s" % report)
    return 0


def lane_gdunit(
    godot: str,
    *,
    paths: list[str],
    lane: str,
    report_dir: Path,
    timeout: float,
) -> int:
    if report_dir.exists():
        shutil.rmtree(report_dir)
    report_dir.mkdir(parents=True)
    argv = [
        "--path",
        str(ROOT),
        "-s",
        "res://addons/gdUnit4/bin/GdUnitCmdTool.gd",
        "--headless",
        "--ignoreHeadlessMode",
        "--ignore-error-breaks",
        "-c",
        "-rc",
        "1",
        "-rd",
        "res://" + str(report_dir.relative_to(ROOT)).replace(os.sep, "/"),
    ]
    for path in paths:
        argv += ["-a", path]
    _godot(
        godot,
        argv,
        log=TMP / ("gdunit-%s.log" % lane),
        timeout=timeout,
        lane=lane,
        check=False,
    )
    produced = sorted(report_dir.glob("report_*/results.xml"))
    if not produced:
        raise fail("the %s lane wrote no results.xml under %s" % (lane, report_dir))
    return 0


def lane_module(*, timeout: float, cases_tag: str) -> int:
    binary = next(
        (
            path
            for path in sorted((ROOT / "extension" / "thirdparty" / "godot" / "bin").glob("godot.*"))
            if path.is_file() and os.access(path, os.X_OK)
        ),
        None,
    )
    if binary is None:
        raise fail("no engine binary under extension/thirdparty/godot/bin")
    listing = run(
        [str(binary), "--headless", "--test", "--test-case=*%s*" % cases_tag, "--list-test-cases"],
        capture=True,
        timeout=timeout,
    )
    if cases_tag not in listing.stdout:
        raise fail("the module binary lists no %s case, so the filter matched nothing" % cases_tag)
    run(
        [str(binary), "--headless", "--test", "--test-case=*%s*" % cases_tag, "--success"],
        timeout=timeout,
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", default=os.environ.get("GODOT", "godot"))
    parser.add_argument("--timeout", type=float, default=1800.0)
    sub = parser.add_subparsers(dest="lane", required=True)

    sub.add_parser("import")
    sub.add_parser("classes")

    consumer = sub.add_parser("consumer")
    consumer.add_argument("--zip", dest="archive", type=Path)
    consumer.add_argument("--zip-glob", dest="archive_glob", help="Resolve the archive by pattern, newest first.")
    consumer.add_argument("--workspace", type=Path, default=ROOT / "dist" / "consumer")

    parse = sub.add_parser("parse")
    parse.add_argument("roots", nargs="*", default=["res://tests", "res://addons/networked"])

    native = sub.add_parser("native")
    native.add_argument("--filter", dest="native_filter")

    gdunit = sub.add_parser("gdunit")
    gdunit.add_argument("--name", required=True, help="Names this lane's report and runtime dirs.")
    gdunit.add_argument("--path", action="append", dest="paths", required=True)
    gdunit.add_argument("--report-dir", type=Path)

    module = sub.add_parser("module")
    module.add_argument("--tag", default="[Networked]")
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.lane == "import":
        return lane_import(args.godot, timeout=args.timeout)
    if args.lane == "classes":
        return lane_classes(args.godot, timeout=args.timeout)
    if args.lane == "consumer":
        archive = args.archive
        if archive is None:
            if not args.archive_glob:
                raise fail("pass --zip or --zip-glob")
            found = sorted(ROOT.glob(args.archive_glob), key=lambda p: p.stat().st_mtime, reverse=True)
            if not found:
                raise fail("no archive matched %s" % args.archive_glob)
            archive = found[0]
        return lane_consumer(
            args.godot,
            archive=archive,
            workspace=args.workspace,
            timeout=args.timeout,
        )
    if args.lane == "parse":
        return lane_parse(args.godot, args.roots, timeout=args.timeout)
    if args.lane == "native":
        return lane_native(args.godot, timeout=args.timeout, native_filter=args.native_filter)
    if args.lane == "gdunit":
        report_dir = args.report_dir or (REPORTS / args.name)
        return lane_gdunit(
            args.godot,
            paths=args.paths,
            lane=args.name,
            report_dir=report_dir,
            timeout=args.timeout,
        )
    return lane_module(timeout=args.timeout, cases_tag=args.tag)


if __name__ == "__main__":
    sys.exit(main(lambda: run_cli()))
