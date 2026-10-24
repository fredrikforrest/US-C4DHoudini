#!/usr/bin/env bash
# Copies the host-neutral core into the Cinema 4D plugin's source tree so the
# Maxon project tool picks it up (it only compiles files under source/).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
dst="$root/src/c4d_plugin/source/hapi_core"
mkdir -p "$dst"
cp "$root"/src/hapi_core/*.h "$root"/src/hapi_core/*.cpp "$root"/src/hapi_core/*.inc "$dst"/
echo "synced hapi_core -> $dst"
