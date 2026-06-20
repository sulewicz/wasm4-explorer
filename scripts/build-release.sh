#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build-release.sh [OUT_DIR]

Builds the end-user OnionOS install archive:
  WASM4-Onion-vX.Y.Z.zip

Default OUT_DIR: build/release

Environment:
  PACKAGE_VERSION  Override version from build/manifest.json.
EOF
}

repo_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="${1:-$repo_dir/build/release}"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

manifest_version() {
  python3 - "$repo_dir/build/manifest.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    print(json.load(handle).get("package_version", ""))
PY
}

json_escape() {
  printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

sha256_of() {
  sha256sum "$1" | awk '{print $1}'
}

size_of() {
  stat -c '%s' "$1"
}

package_version="${PACKAGE_VERSION:-$(manifest_version)}"
package_version="${package_version:-0.1.0}"
version_label="${package_version#v}"
archive_name="WASM4-Onion-v$version_label.zip"
archive_path="$out_dir/$archive_name"
manifest_path="$out_dir/release-manifest.json"
stage_dir="$out_dir/stage"
stage_root="$stage_dir/root"
runtime_out="$out_dir/package-manager-runtime"
explorer_out="$out_dir/package-manager-explorer"

rm -rf "$stage_dir" "$runtime_out" "$explorer_out"
mkdir -p "$stage_root/App" "$out_dir"

PACKAGE_VERSION="$package_version" "$repo_dir/scripts/build-package-manager-runtime.sh" "$runtime_out" >/tmp/wasm4-release-runtime-build.log
PACKAGE_VERSION="$package_version" "$repo_dir/scripts/build-package-manager-explorer.sh" "$explorer_out" >/tmp/wasm4-release-explorer-build.log

cp -a "$runtime_out/App/." "$stage_root/App/"
cp -a "$explorer_out/App/." "$stage_root/App/"

required_paths='
App/PackageManager/data/Emu/WASM-4 (WASM-4)/Emu/WASM4/config.json
App/PackageManager/data/Emu/WASM-4 (WASM-4)/RetroArch/.retroarch/cores/wasm4_libretro.so
App/PackageManager/data/App/WASM-4 Explorer/App/WASM4Explorer/bin/wasm4-explorer
App/PackageManager/data/App/WASM-4 Explorer/App/WASM4Explorer/launch.sh
'

printf '%s\n' "$required_paths" | while IFS= read -r rel_path; do
  [ -n "$rel_path" ] || continue
  if [ ! -e "$stage_root/$rel_path" ]; then
    echo "Missing release payload path: $rel_path" >&2
    exit 1
  fi
done

rm -f "$archive_path" "$archive_path.sha256" "$manifest_path"
python3 - "$stage_root" "$archive_path" <<'PY'
import os
import stat
import sys
import zipfile

stage_root, archive_path = sys.argv[1:]
with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for root, dirs, files in os.walk(stage_root):
        dirs.sort()
        files.sort()
        for name in files:
            path = os.path.join(root, name)
            rel = os.path.relpath(path, stage_root).replace(os.sep, "/")
            info = zipfile.ZipInfo(rel)
            mode = stat.S_IMODE(os.stat(path).st_mode)
            info.external_attr = mode << 16
            with open(path, "rb") as handle:
                archive.writestr(info, handle.read(), compress_type=zipfile.ZIP_DEFLATED)
PY

archive_sha="$(sha256_of "$archive_path")"
archive_size="$(size_of "$archive_path")"
printf '%s  %s\n' "$archive_sha" "$archive_name" > "$archive_path.sha256"

{
  printf '{\n'
  printf '  "package_version": "%s",\n' "$(json_escape "$package_version")"
  printf '  "install_archive": {\n'
  printf '    "file": "%s",\n' "$(json_escape "$archive_name")"
  printf '    "sha256": "%s",\n' "$archive_sha"
  printf '    "size_bytes": %s\n' "$archive_size"
  printf '  },\n'
  printf '  "package_manager_entries": [\n'
  printf '    "App/PackageManager/data/Emu/WASM-4 (WASM-4)",\n'
  printf '    "App/PackageManager/data/App/WASM-4 Explorer"\n'
  printf '  ]\n'
  printf '}\n'
} > "$manifest_path"

rm -rf "$stage_dir"

echo "Built release archive:"
echo "$archive_path"
echo "$archive_path.sha256"
echo "$manifest_path"
