#!/usr/bin/env python3
"""
Filter out private _-prefixed members and non-class scripts from Godot doctool XML.
Used as a pre-processing step before make_rst.py.
"""
from __future__ import annotations

import argparse
import os
import sys
import xml.etree.ElementTree as ET


def should_skip_class(root: ET.Element) -> bool:
    """Return True if this XML should be excluded from docs entirely."""
    name = root.get("name", "")
    # Files without a class_name declaration get a quoted file path as name.
    # These are editor plugins, UI scripts, vendored addons -- not public API.
    if name.startswith('"'):
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
        description="Filter private _-prefixed members from Godot doctool XML."
    )
    parser.add_argument("input_dir", help="Directory containing XML files.")
    parser.add_argument("output_dir", help="Directory to write filtered XML files.")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    for filename in os.listdir(args.input_dir):
        if not filename.endswith(".xml"):
            continue

        input_path = os.path.join(args.input_dir, filename)
        output_path = os.path.join(args.output_dir, filename)

        tree = ET.parse(input_path)
        root = tree.getroot()

        if should_skip_class(root):
            print(f"Skipped: {filename}")
            continue

        filter_private_elements(root)
        tree.write(output_path, encoding="utf-8", xml_declaration=True)

        print(f"Filtered: {filename}")

    print("Done.")


if __name__ == "__main__":
    main()
