#!/usr/bin/env python3
"""Run a named local profile and write down what it actually proved."""

from __future__ import annotations

import argparse
import json
import shutil
import time
from pathlib import Path

from common import CI_DIR, ROOT, fail, git_dirty, git_head, main, run
from bootstrap import host_record

EVIDENCE = ROOT / "dist" / "evidence.json"
ACT_IMAGE = "catthehacker/ubuntu:act-latest"

PROFILES = {
    "checks": [
        ["python3", "ci/reports.py", "--self-test"],
        ["python3", "-m", "unittest", "discover", "-s", "ci/tests", "-t", "."],
        ["python3", "ci/matrix.py", "--profile", "release", "--format", "ids"],
        ["python3", "ci/bootstrap.py", "tools", "actionlint", "shellcheck"],
        ["ci/.tools/actionlint"],
        ["ci/.tools/shellcheck", "--shell=sh", "extension/tools/setup_godot.sh"],
    ],
    "native": [
        [
            "python3",
            "ci/build.py",
            "--cell",
            "linux:x86_64:template_debug",
            "--no-staging",
            "--scons-arg",
            "netw_tests=yes",
        ],
        ["python3", "ci/test.py", "native"],
        ["python3", "ci/reports.py", "reports/native/results.xml", "--floor", "tests/native/hosted_floor.txt"],
    ],
    "gdscript": [
        ["python3", "ci/test.py", "parse", "res://tests", "res://addons/networked"],
        ["python3", "ci/test.py", "gdunit", "--name", "tests", "--path", "res://tests/"],
        ["python3", "ci/reports.py", "reports/tests/report_1", "--log", "tmp/gdunit-tests.log", "--allow-stale"],
    ],
    "examples": [
        ["python3", "ci/test.py", "--timeout", "2400", "gdunit", "--name", "examples", "--path", "res://examples/"],
        [
            "python3",
            "ci/reports.py",
            "reports/examples/report_1",
            "--log",
            "tmp/gdunit-examples.log",
            "--allow-stale",
        ],
    ],
    "module": [
        ["extension/tools/setup_godot.sh"],
        ["python3", "ci/test.py", "module"],
    ],
    "web": [
        ["python3", "ci/build.py", "--cell", "web:wasm32:template_release"],
        ["python3", "ci/engine.py", "templates", "--dest", ".engines/export_templates"],
        [
            "python3",
            "ci/export.py",
            "web",
            "--preset",
            "Web",
            "--scene",
            "uid://3a3852p4fypv",
            "--out",
            "build/web",
            "--templates",
            ".engines/export_templates",
            "--record",
            "dist/web-export.json",
        ],
        ["python3", "ci/export.py", "smoke", "--dir", "build/web"],
    ],
    "candidate": [
        ["python3", "ci/build.py", "--profile", "local", "--strip"],
        ["python3", "ci/package.py", "--profile", "local"],
        ["python3", "ci/release.py", "--profile", "local", "--record", "dist/inventory.json"],
        ["python3", "ci/test.py", "consumer", "--zip-glob", "dist/networked-*.zip"],
    ],
}


def act_rehearsal(cell: str, *, image: str, artifacts: Path, timeout: float, job: str) -> None:
    """Exercise the workflow layer itself, not a second copy of the commands."""
    act = CI_DIR / ".tools" / "act"
    if not act.is_file():
        run(["python3", "ci/bootstrap.py", "tools", "act"], timeout=600)
    if artifacts.exists():
        shutil.rmtree(artifacts)
    artifacts.mkdir(parents=True)
    argv = [
        str(act),
        "workflow_dispatch",
        "-W",
        ".github/workflows/gdextension.yml",
        "-P",
        "ubuntu-22.04=%s" % image,
        "--pull=false",
        "--artifact-server-path",
        str(artifacts),
        "--input",
        "cells=%s" % cell,
    ]
    if job:
        argv[4:4] = ["-j", job]
    run(argv, timeout=timeout)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profiles", nargs="*", default=["checks"], choices=sorted(PROFILES) + [])
    parser.add_argument("--evidence", type=Path, default=EVIDENCE)
    parser.add_argument("--timeout", type=float, default=3600.0)
    parser.add_argument("--act-cell", default="")
    parser.add_argument("--act-image", default=ACT_IMAGE)
    parser.add_argument("--act-job", default="", help="Empty runs the whole build and package chain.")
    parser.add_argument("--keep-going", action="store_true")
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    steps: list[dict] = []
    refused = 0

    for name in args.profiles:
        if name not in PROFILES:
            raise fail("unknown profile '%s'; known: %s" % (name, ", ".join(sorted(PROFILES))))
        for command in PROFILES[name]:
            started = time.time()
            try:
                completed = run(command, timeout=args.timeout, check=False)
                code = completed.returncode
            except Exception as error:
                print("::error::%s" % error)
                code = 1
            steps.append(
                {
                    "profile": name,
                    "command": command,
                    "exit": code,
                    "seconds": round(time.time() - started, 1),
                }
            )
            if code != 0:
                refused += 1
                if not args.keep_going:
                    break
        if refused and not args.keep_going:
            break

    if args.act_cell and not refused:
        started = time.time()
        try:
            act_rehearsal(
                args.act_cell,
                image=args.act_image,
                artifacts=ROOT / "dist" / "act-artifacts",
                timeout=args.timeout,
                job=args.act_job,
            )
            code = 0
        except Exception as error:
            print("::error::%s" % error)
            code = 1
            refused += 1
        steps.append(
            {
                "profile": "act",
                "command": ["act", "gdextension.yml:%s" % (args.act_job or "all"), args.act_cell],
                "exit": code,
                "seconds": round(time.time() - started, 1),
            }
        )

    evidence = {
        "recorded": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source": {"sha": git_head(), "dirty": git_dirty()},
        "host": host_record(),
        "profiles": args.profiles,
        "steps": steps,
        "refused": refused,
    }
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    for step in steps:
        print("%-10s %-4s %6.1fs  %s" % (step["profile"], step["exit"], step["seconds"], " ".join(step["command"])))
    print("RESULT %s" % args.evidence)
    return 1 if refused else 0


if __name__ == "__main__":
    raise SystemExit(main(lambda: run_cli()))
