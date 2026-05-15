#!/usr/bin/env python3
"""
Filter Godot doctool XML to only include classes from addons/networked.

Two-pass approach:
1. Scan addons/networked/ source files to build an allowlist of:
   - class_name declarations (e.g. "TubeBackend", "Netw")
   - autoload names from project.godot (e.g. "NetworkedDebugger")
   - auto-named scripts without class_name (e.g. "addons/networked/plugin.gd")
   - inner classes (e.g. "TubeBackend.TubeWrapper")
2. Copy only matching XML files from api/ to api_filtered/, filtering
   private _-prefixed members along the way.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

CLASS_NAME_RE = re.compile(r"^\s*class_name\s+(\w[\w.]*)\s*$", re.MULTILINE)


def _relative_to_project(gd_path: Path, project_root: Path) -> str | None:
    """Return the path relative to the project root, or None if not under it."""
    try:
        return str(gd_path.relative_to(project_root)).replace("\\", "/")
    except ValueError:
        return None


def _find_project_root(addon_dir: str) -> Path:
    """Walk up from addon_dir to find project.godot."""
    current = Path(addon_dir).resolve()
    for _ in range(10):
        if (current / "project.godot").is_file():
            return current
        parent = current.parent
        if parent == current:
            break
        current = parent
    return Path(addon_dir).resolve().parent.parent


def scan_addon_for_class_names(addon_dir: str) -> tuple[set[str], set[str]]:
    """
    Walk addon_dir and return:
      - allowed_names: set of class_name values + autoload names
      - allowed_auto: set of auto-named file paths (e.g. {"addons/networked/plugin.gd", ...})
    """
    allowed_names: set[str] = set()
    allowed_auto: set[str] = set()

    project_root = _find_project_root(addon_dir)
    addon_root = Path(addon_dir).resolve()

    # Scan project.godot for autoload names
    project_file = project_root / "project.godot"
    if project_file.is_file():
        try:
            content = project_file.read_text(encoding="utf-8", errors="replace")
            in_autoload = False
            for line in content.splitlines():
                stripped = line.strip()
                if stripped == "[autoload]":
                    in_autoload = True
                    continue
                if stripped.startswith("[") and in_autoload:
                    break
                if in_autoload and "=" in stripped:
                    name = stripped.split("=")[0].strip()
                    if name and not name.startswith(";") and not name.startswith("#"):
                        allowed_names.add(name)
        except Exception:
            pass

    # Scan all .gd files under addon_dir for class_name declarations
    for root, _dirs, files in os.walk(addon_root):
        for filename in files:
            if not filename.endswith(".gd"):
                continue

            gd_path = Path(root) / filename
            rel = _relative_to_project(gd_path, project_root)
            if rel is None:
                continue

            try:
                content = gd_path.read_text(encoding="utf-8", errors="replace")
            except Exception:
                continue

            matches = CLASS_NAME_RE.findall(content)
            if matches:
                for name in matches:
                    allowed_names.add(name)
            else:
                allowed_auto.add(rel)

    return allowed_names, allowed_auto


def _is_allowed(root: ET.Element, allowed_names: set[str], allowed_auto: set[str]) -> bool:
    """Return True if this XML element matches the allowlist."""
    name = root.get("name", "")

    # Auto-named scripts (no class_name) use a quoted file path
    if name.startswith('"'):
        quoted_path = name.strip('"')
        return quoted_path in allowed_auto

    # Explicit class_name or inner class (e.g. "TubeBackend.TubeWrapper")
    if name in allowed_names:
        return True

    parent_class = name.split(".")[0]
    if parent_class in allowed_names:
        return True

    return False


def filter_private_elements(root: ET.Element) -> None:
    """Remove any member element whose name starts with '_'."""
    parent_tags = [
        "members", "constructors", "methods", "operators",
        "signals", "constants", "annotations",
    ]
    child_tags = [
        "member", "constructor", "method", "operator",
        "signal", "constant", "annotation",
    ]

    for parent_tag in parent_tags:
        parent = root.find(parent_tag)
        if parent is None:
            continue
        for child_tag in child_tags:
            for elem in parent.findall(child_tag):
                name = elem.get("name", "")
                if name.startswith("_"):
                    parent.remove(elem)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Filter Godot doctool XML to only include classes from addons/networked."
    )
    parser.add_argument("input_dir", help="Directory containing XML files.")
    parser.add_argument("output_dir", help="Directory to write filtered XML files.")
    parser.add_argument(
        "--addon-dir",
        default=None,
        help="Path to the addon directory to scan for class_name declarations "
             "(e.g. addons/networked). Defaults to addons/networked relative to project root.",
    )
    args = parser.parse_args()

    if args.addon_dir is None:
        # input_dir is typically docs/api; project root is two levels up
        project_root = Path(args.input_dir).resolve().parent.parent
        args.addon_dir = str(project_root / "addons" / "networked")

    print(f"Scanning addon: {args.addon_dir}")
    allowed_names, allowed_auto = scan_addon_for_class_names(args.addon_dir)
    print(f"Found {len(allowed_names)} allowed class names: {sorted(allowed_names)}")
    print(f"Found {len(allowed_auto)} auto-named scripts")

    os.makedirs(args.output_dir, exist_ok=True)

    kept = 0
    skipped = 0

    for filename in sorted(os.listdir(args.input_dir)):
        if not filename.endswith(".xml"):
            continue

        input_path = os.path.join(args.input_dir, filename)
        output_path = os.path.join(args.output_dir, filename)

        try:
            tree = ET.parse(input_path)
            root = tree.getroot()

            if not _is_allowed(root, allowed_names, allowed_auto):
                print(f"Skipped: {filename}")
                skipped += 1
                continue

            filter_private_elements(root)
            tree.write(output_path, encoding="utf-8", xml_declaration=True)
            print(f"Filtered: {filename}")
            kept += 1
        except ET.ParseError as e:
            print(f"Error parsing {filename}: {e}. Skipping file.")
        except Exception as e:
            print(f"Unexpected error processing {filename}: {e}. Skipping file.")

    print(f"\nDone. Kept {kept} files, skipped {skipped} files.")


if __name__ == "__main__":
    main()
