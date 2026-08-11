#!/usr/bin/env sh
# Clones the engine pinned in deps.env (unless GODOT_SRC points at an existing
# checkout) and mounts the extension as the engine module `networked`.
#
# The mount is a directory of per-entry links rather than one link to
# extension/, because extension/ contains the vendored engine: a single link
# would make the mount contain itself and every symlink-following walker
# would loop.
set -eu

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
EXT="$REPO/extension"
GODOT_SRC="${GODOT_SRC:-$EXT/thirdparty/godot}"
GODOT_REF="$(grep '^GODOT_REF=' "$EXT/deps.env" | cut -d= -f2)"

if [ ! -d "$GODOT_SRC" ]; then
    git clone --depth 1 --branch "$GODOT_REF" \
        https://github.com/godotengine/godot.git "$GODOT_SRC"
fi

# version.py rather than git describe: shallow clones carry no tags.
major="$(sed -n 's/^major = //p' "$GODOT_SRC/version.py")"
minor="$(sed -n 's/^minor = //p' "$GODOT_SRC/version.py")"
patch="$(sed -n 's/^patch = //p' "$GODOT_SRC/version.py")"
status="$(sed -n 's/^status = "\(.*\)"/\1/p' "$GODOT_SRC/version.py")"
version="$major.$minor"
[ "$patch" != "0" ] && version="$version.$patch"
version="$version-$status"
if [ "$version" != "$GODOT_REF" ]; then
    echo "WARNING: engine at $GODOT_SRC is $version, not $GODOT_REF." \
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
