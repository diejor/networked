#!/usr/bin/env sh
set -eu

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
EXT="$REPO/extension"
GODOT_SRC="${GODOT_SRC:-$EXT/thirdparty/godot}"
ENGINE="${NETW_ENGINE:-}"

catalog() {
    if [ -n "$ENGINE" ]; then
        python3 "$REPO/ci/engine.py" --engine "$ENGINE" source "$1"
    else
        python3 "$REPO/ci/engine.py" source "$1"
    fi
}

GODOT_REPO="$(catalog repo)"
GODOT_BRANCH="$(catalog branch)"
GODOT_VERSION="$(catalog version)"

if [ ! -d "$GODOT_SRC" ]; then
    git clone --depth 1 --branch "$GODOT_BRANCH" "$GODOT_REPO" "$GODOT_SRC"
fi

major="$(sed -n 's/^major = //p' "$GODOT_SRC/version.py")"
minor="$(sed -n 's/^minor = //p' "$GODOT_SRC/version.py")"
patch="$(sed -n 's/^patch = //p' "$GODOT_SRC/version.py")"
status="$(sed -n 's/^status = "\(.*\)"/\1/p' "$GODOT_SRC/version.py")"
version="$major.$minor"
[ "$patch" != "0" ] && version="$version.$patch"
version="$version-$status"
if [ "$version" != "$GODOT_VERSION" ]; then
    echo "WARNING: engine at $GODOT_SRC is $version, not $GODOT_VERSION." \
        "The module is only certified against the pin." >&2
fi

MOUNT="$GODOT_SRC/modules/networked"
rm -rf "$MOUNT"
mkdir -p "$MOUNT"
for entry in config.py SCsub sources.py register_types.h register_types.cpp \
        src tests doc_classes; do
    ln -s "$EXT/$entry" "$MOUNT/$entry"
done

test -f "$MOUNT/config.py"
test -f "$MOUNT/SCsub"
test -f "$MOUNT/register_types.h"
