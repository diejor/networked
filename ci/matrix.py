#!/usr/bin/env python3
"""Resolve a build profile into an explicit list of library cells."""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

from common import CI_DIR, load_json, fail, github_output, main

PLATFORMS_FILE = CI_DIR / "platforms.json"

SHARED_SUFFIX = {
    "linux": ".so",
    "android": ".so",
    "windows": ".dll",
    "web": ".wasm",
    "ios": ".dylib",
}

PACKAGE_KINDS = ("shared_object", "wasm", "framework", "xcframework")
STRIP_KINDS = ("none", "elf", "ndk")


@dataclass
class Cell:
    platform: str
    slice: str
    arch: str
    target: str
    runner: str
    package: str
    strip: str
    tag: str = ""
    simulator: bool = False
    threads: bool = True
    precision: str = "single"
    flags: list[str] = field(default_factory=list)

    @property
    def id(self) -> str:
        return "%s:%s:%s" % (self.platform, self.slice, self.target)

    @property
    def suffix(self) -> str:
        """The name godot-cpp gives this configuration."""
        suffix = ".%s.%s" % (self.platform, self.target)
        if self.precision == "double":
            suffix += ".double"
        suffix += "." + self.arch
        if self.simulator:
            suffix += ".simulator"
        if not self.threads:
            suffix += ".nothreads"
        return suffix

    @property
    def artifact(self) -> str:
        parts = ("native", self.platform, self.arch, self.tag, self.target)
        return "-".join(part for part in parts if part)

    @property
    def outputs(self) -> list[str]:
        """Every path this cell must leave in the addon bin directory."""
        if self.package == "framework":
            base = "libnetworked.%s.%s.framework" % (self.platform, self.target)
            return [base]
        return ["libnetworked%s%s" % (self.suffix, SHARED_SUFFIX[self.platform])]

    @property
    def scons_args(self) -> list[str]:
        args = [
            "platform=%s" % self.platform,
            "target=%s" % self.target,
            "arch=%s" % self.arch,
            "precision=%s" % self.precision,
        ]
        args.extend(self.flags)
        return args

    def to_json(self) -> dict:
        payload = asdict(self)
        payload.update(
            id=self.id,
            suffix=self.suffix,
            artifact=self.artifact,
            outputs=self.outputs,
            scons_args=self.scons_args,
        )
        return payload


def load_catalog(path: Path | None = None) -> dict:
    catalog = load_json(path or PLATFORMS_FILE)
    if catalog.get("schema") != 1:
        raise fail("platforms.json declares an unknown schema")
    for name, platform in catalog["platforms"].items():
        if platform["package"] not in PACKAGE_KINDS:
            raise fail("platform '%s' has unknown package kind '%s'" % (name, platform["package"]))
        if platform["strip"] not in STRIP_KINDS:
            raise fail("platform '%s' has unknown strip kind '%s'" % (name, platform["strip"]))
        if platform["package"] != "framework" and name not in SHARED_SUFFIX:
            raise fail("platform '%s' has no library suffix" % name)
        seen = set()
        for spec in platform["slices"]:
            if spec["slice"] in seen:
                raise fail("platform '%s' declares slice '%s' twice" % (name, spec["slice"]))
            seen.add(spec["slice"])
    return catalog


def _cell(catalog: dict, platform_name: str, slice_name: str, target: str) -> Cell:
    platforms = catalog["platforms"]
    if platform_name not in platforms:
        raise fail("unsupported platform '%s'; known: %s" % (platform_name, ", ".join(sorted(platforms))))
    platform = platforms[platform_name]
    slices = {spec["slice"]: spec for spec in platform["slices"]}
    if slice_name not in slices:
        raise fail(
            "platform '%s' has no slice '%s'; known: %s" % (platform_name, slice_name, ", ".join(sorted(slices)))
        )
    if target not in platform["targets"]:
        raise fail(
            "platform '%s' does not build target '%s'; known: %s"
            % (platform_name, target, ", ".join(platform["targets"]))
        )
    spec = slices[slice_name]
    return Cell(
        platform=platform_name,
        slice=slice_name,
        arch=spec["arch"],
        target=target,
        runner=spec["runner"],
        package=platform["package"],
        strip=platform["strip"],
        tag=spec.get("tag", ""),
        simulator=bool(spec.get("simulator", False)),
        threads=bool(spec.get("threads", True)),
        precision=catalog.get("precision", "single"),
        flags=list(platform.get("flags", [])) + list(spec.get("flags", [])),
    )


def resolve(
    catalog: dict,
    *,
    profile: str | None = None,
    cells: list[str] | None = None,
    platforms: list[str] | None = None,
    targets: list[str] | None = None,
) -> list[Cell]:
    requested: list[str] = list(cells or [])
    if profile:
        if profile not in catalog["profiles"]:
            raise fail("unknown profile '%s'; known: %s" % (profile, ", ".join(sorted(catalog["profiles"]))))
        spec = catalog["profiles"][profile]
        requested.extend(spec.get("cells", []))
        platforms = list(platforms or []) + list(spec.get("platforms", []))
        targets = list(targets or []) + list(spec.get("targets", []))

    resolved: list[Cell] = []
    for entry in requested:
        parts = entry.split(":")
        if len(parts) != 3:
            raise fail("cell '%s' is not platform:slice:target" % entry)
        resolved.append(_cell(catalog, *parts))

    for platform_name in platforms or []:
        if platform_name not in catalog["platforms"]:
            raise fail(
                "unsupported platform '%s'; known: %s" % (platform_name, ", ".join(sorted(catalog["platforms"])))
            )
        platform = catalog["platforms"][platform_name]
        wanted = [t for t in (targets or platform["targets"]) if t in platform["targets"]]
        if targets and not wanted:
            raise fail("platform '%s' builds none of the requested targets %s" % (platform_name, ", ".join(targets)))
        for spec in platform["slices"]:
            for target in wanted:
                resolved.append(_cell(catalog, platform_name, spec["slice"], target))

    seen: dict[str, Cell] = {}
    for cell in resolved:
        if cell.id in seen:
            raise fail("cell '%s' was requested twice" % cell.id)
        seen[cell.id] = cell
    if not seen:
        raise fail("the request resolved to no cells")
    return list(seen.values())


def parse_list(value: str | None) -> list[str]:
    if not value:
        return []
    return [part for part in value.replace(" ", "").split(",") if part]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile")
    parser.add_argument("--cells", default="")
    parser.add_argument("--platforms", default="")
    parser.add_argument("--targets", default="")
    parser.add_argument("--catalog", type=Path, default=PLATFORMS_FILE)
    parser.add_argument(
        "--format",
        choices=("json", "github", "ids", "runners"),
        default="json",
    )
    return parser


def run(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    catalog = load_catalog(args.catalog)
    cells = resolve(
        catalog,
        profile=args.profile,
        cells=parse_list(args.cells),
        platforms=parse_list(args.platforms),
        targets=parse_list(args.targets),
    )
    payload = [cell.to_json() for cell in cells]
    if args.format == "json":
        print(json.dumps(payload, indent=2))
    elif args.format == "github":
        encoded = json.dumps(payload, separators=(",", ":"))
        print("cells=" + encoded)
        github_output(cells=encoded, count=str(len(cells)))
    elif args.format == "ids":
        for cell in cells:
            print(cell.id)
    else:
        for runner in sorted({cell.runner for cell in cells}):
            print(runner)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(lambda: run()))
