#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build-package-manager-explorer.sh [OUT_DIR]

Builds an Onion Package Manager-compatible WASM-4 Explorer app package from
package-root/. The generated output is ignored by git and can be copied to an
OnionOS SD card root for installation through Package Manager.

Default OUT_DIR: build/package-manager-explorer
EOF
}

repo_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="${1:-$repo_dir/build/package-manager-explorer}"
package_name="WASM-4 Explorer"
package_dir="$out_dir/App/PackageManager/data/App/$package_name"
src="$repo_dir/package-root"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

require_file() {
  if [ ! -f "$src/$1" ]; then
    echo "Missing package source file: package-root/$1" >&2
    exit 1
  fi
}

manifest_version() {
  sed -n 's/^[[:space:]]*"package_version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$repo_dir/build/manifest.json" | sed -n '1p'
}

require_file "App/WASM4Explorer/config.json"
require_file "App/WASM4Explorer/core-manifest.json"
require_file "App/WASM4Explorer/launch.sh"
require_file "App/WASM4Explorer/bin/check_core.sh"
require_file "App/WASM4Explorer/bin/update_core.sh"
require_file "App/WASM4Explorer/LICENSES/project_LICENSE.txt"
require_file "Icons/Default/app/wasm4_explorer.png"

package_version="${PACKAGE_VERSION:-$(manifest_version)}"
package_version="${package_version:-0.1.0}"
native_build_dir="$out_dir/native-explorer-build"
PACKAGE_VERSION="$package_version" "$repo_dir/scripts/build-native-explorer.sh" target "$native_build_dir"
native_bin="$native_build_dir/miyoomini/wasm4-explorer"
[ -x "$native_bin" ] || {
  echo "Native Explorer target build did not produce $native_bin" >&2
  exit 1
}

rm -rf "$package_dir"
mkdir -p "$package_dir/App" "$package_dir/Icons/Default/app"
cp -a "$src/App/WASM4Explorer" "$package_dir/App/"
cp -a "$src/Icons/Default/app/wasm4_explorer.png" "$package_dir/Icons/Default/app/"
cp "$native_bin" "$package_dir/App/WASM4Explorer/bin/wasm4-explorer"

python3 - "$package_dir/App/WASM4Explorer/core-manifest.json" "$package_version" <<'PY'
import json
import sys

path, package_version = sys.argv[1:]
with open(path, encoding="utf-8") as handle:
    data = json.load(handle)
data["package_version"] = package_version
with open(path, "w", encoding="utf-8") as handle:
    json.dump(data, handle, indent=2)
    handle.write("\n")
PY

chmod 755 \
  "$package_dir/App/WASM4Explorer/launch.sh" \
  "$package_dir/App/WASM4Explorer/bin/wasm4-explorer" \
  "$package_dir/App/WASM4Explorer/bin/check_core.sh" \
  "$package_dir/App/WASM4Explorer/bin/update_core.sh"

legacy_paths='
App/WASM4Explorer/bin/advcommand.sh
App/WASM4Explorer/bin/build_explorer_menu.sh
App/WASM4Explorer/bin/download_and_launch.sh
App/WASM4Explorer/bin/prefetch_images.sh
App/WASM4Explorer/bin/refresh_catalog.sh
App/WASM4Explorer/bin/sanitize_filename.sh
App/WASM4Explorer/bin/update_miyoogamelist.sh
App/WASM4Explorer/cache/entries
'

printf '%s\n' "$legacy_paths" | while IFS= read -r rel_path; do
  [ -n "$rel_path" ] || continue
  if [ -e "$package_dir/$rel_path" ]; then
    echo "Generated Explorer package contains retired prototype artifact: $rel_path" >&2
    exit 1
  fi
done

find "$package_dir/App/WASM4Explorer/cache" "$package_dir/App/WASM4Explorer/logs" -type f ! -name '.gitkeep' -print | grep -q . && {
  echo "Generated Explorer package contains runtime cache or log files" >&2
  exit 1
}

find "$package_dir/App/WASM4Explorer/catalog" -type f ! -name '.gitkeep' -print | grep -q . && {
  echo "Generated Explorer package contains bundled catalog manifests" >&2
  exit 1
}

find "$package_dir" -type f | sort > "$out_dir/manifest.files"

echo "Built Package Manager Explorer app package:"
echo "$package_dir"
