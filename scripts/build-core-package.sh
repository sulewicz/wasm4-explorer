#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build-core-package.sh [OUT_DIR]

Builds the core-only release asset:
  WASM4-Onion-core-vX.Y.Z.tar.gz

Default OUT_DIR: build/core-package

Environment:
  PACKAGE_VERSION  Override version from build/manifest.json.
EOF
}

repo_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="${1:-$repo_dir/build/core-package}"
src="$repo_dir/package-root"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

manifest_version() {
  sed -n 's/^[[:space:]]*"package_version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$repo_dir/build/manifest.json" | sed -n '1p'
}

require_file() {
  if [ ! -f "$src/$1" ]; then
    echo "Missing package source file: package-root/$1" >&2
    exit 1
  fi
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
archive_name="WASM4-Onion-core-v$version_label.tar.gz"
archive_path="$out_dir/$archive_name"
stage_dir="$out_dir/stage"
manifest_path="$out_dir/release-manifest.json"

payload_paths=(
  "RetroArch/.retroarch/cores/wasm4_libretro.so"
  "RetroArch/.retroarch/cores/wasm4_libretro.info"
  "RetroArch/.retroarch/info/wasm4_libretro.info"
  "RetroArch/.retroarch/core_info/wasm4_libretro.info"
)

for path in "${payload_paths[@]}"; do
  require_file "$path"
done

rm -rf "$stage_dir"
mkdir -p "$stage_dir" "$out_dir"

for path in "${payload_paths[@]}"; do
  install -D -m 0644 "$src/$path" "$stage_dir/$path"
done

rm -f "$archive_path" "$archive_path.sha256" "$manifest_path"
tar -C "$stage_dir" -czf "$archive_path" "${payload_paths[@]}"

archive_sha="$(sha256_of "$archive_path")"
archive_size="$(size_of "$archive_path")"
core_sha="$(sha256_of "$src/RetroArch/.retroarch/cores/wasm4_libretro.so")"
cores_info_sha="$(sha256_of "$src/RetroArch/.retroarch/cores/wasm4_libretro.info")"
info_sha="$(sha256_of "$src/RetroArch/.retroarch/info/wasm4_libretro.info")"
core_info_sha="$(sha256_of "$src/RetroArch/.retroarch/core_info/wasm4_libretro.info")"

printf '%s  %s\n' "$archive_sha" "$archive_name" > "$archive_path.sha256"

{
  printf '{\n'
  printf '  "package_version": "%s",\n' "$(json_escape "$package_version")"
  printf '  "core_package": {\n'
  printf '    "file": "%s",\n' "$(json_escape "$archive_name")"
  printf '    "url": "%s",\n' "$(json_escape "${CORE_PACKAGE_URL:-$archive_name}")"
  printf '    "sha256": "%s",\n' "$archive_sha"
  printf '    "size_bytes": %s,\n' "$archive_size"
  printf '    "contents": [\n'
  for i in "${!payload_paths[@]}"; do
    comma=","
    if [ "$i" -eq "$((${#payload_paths[@]} - 1))" ]; then
      comma=""
    fi
    printf '      "%s"%s\n' "$(json_escape "${payload_paths[$i]}")" "$comma"
  done
  printf '    ]\n'
  printf '  },\n'
  printf '  "core_manifest": {\n'
  printf '    "core_sha256": "%s",\n' "$core_sha"
  printf '    "cores_info_sha256": "%s",\n' "$cores_info_sha"
  printf '    "info_sha256": "%s",\n' "$info_sha"
  printf '    "core_info_sha256": "%s"\n' "$core_info_sha"
  printf '  }\n'
  printf '}\n'
} > "$manifest_path"

rm -rf "$stage_dir"

echo "Built core package:"
echo "$archive_path"
echo "$archive_path.sha256"
echo "$manifest_path"
