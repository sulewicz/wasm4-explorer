#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build-source-package.sh [OUT_DIR]

Builds the source/build release asset:
  WASM4-Onion-source-vX.Y.Z.tar.gz

Default OUT_DIR: build/source-package

Environment:
  PACKAGE_VERSION  Override version from build/manifest.json.
EOF
}

repo_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="${1:-$repo_dir/build/source-package}"

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

manifest_version() {
  python3 - "$repo_dir/build/manifest.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as fh:
    print(json.load(fh).get("package_version", ""))
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

copy_file() {
  src_rel="$1"
  dest="$stage_root/$src_rel"

  if [ -f "$repo_dir/$src_rel" ]; then
    mkdir -p "$(dirname "$dest")"
    cp -p "$repo_dir/$src_rel" "$dest"
  fi
}

copy_tree_files() {
  root_rel="$1"
  [ -d "$repo_dir/$root_rel" ] || return 0

  find "$repo_dir/$root_rel" -type f | sort | while IFS= read -r src_path; do
    rel_path="${src_path#$repo_dir/}"
    case "$rel_path" in
      package-root/App/WASM4Explorer/cache/*|\
      package-root/App/WASM4Explorer/logs/*|\
      package-root/Roms/WASM4/*.wasm|\
      package-root/Roms/WASM4/Imgs/*|\
      package-root/Roms/WASM4/.wasm4/*|\
      package-root/Roms/WASM4/miyoogamelist.xml|\
      package-root/Roms/WASM4/WASM4_cache*.db|\
      package-root/RetroArch/.retroarch/cores/wasm4_libretro.so)
        continue
        ;;
    esac
    copy_file "$rel_path"
  done
}

write_readme_build() {
  python3 - "$repo_dir/build/manifest.json" "$stage_root/README-build.md" <<'PY'
import json
import sys

manifest_path, output_path = sys.argv[1:]
with open(manifest_path, encoding="utf-8") as fh:
    data = json.load(fh)

wasm4 = data.get("wasm4", {})
onion = data.get("onion_validation", {})
core_info = data.get("libretro_core_info", {})
toolchain = data.get("toolchain", {})
artifacts = data.get("artifacts", {})
project = data.get("project", {})

text = f"""# WASM-4 Onion Source Build

Package version: {data.get("package_version", "")}
Build date: {data.get("build_date", "")}
Repository commit: {data.get("repository_commit", "")}

## Inputs

- Project license: {project.get("license", "")}
- WASM-4 checkout: {wasm4.get("source", "")}
- WASM-4 commit: {wasm4.get("commit", "")}
- WASM backend: {wasm4.get("wasm_backend", "")}
- wasm3 commit: {wasm4.get("wasm3_commit", "")}
- Onion validation source: {onion.get("source", "")}
- Onion baseline: {onion.get("commit_or_release", "")}
- RetroArch baseline: {onion.get("retroarch_baseline", "")}
- libretro-core-info source: {core_info.get("source", "")}
- libretro-core-info commit: {core_info.get("commit", "")}

## Toolchain

- Name: {toolchain.get("name", "")}
- Image: {toolchain.get("image", "")}
- Image digest: {toolchain.get("image_digest", "")}
- Compiler: {toolchain.get("compiler", "")}
- Target: {toolchain.get("target", "")}
- CFLAGS: {toolchain.get("cflags", "")}

{toolchain.get("note", "")}

## Rebuild

1. Check out the WASM-4 source at the commit above.
2. From this source package, run `WASM4_DIR=/path/to/wasm4 build/build-wasm4-libretro-miyoo.sh`.
3. Verify artifact checksums against `build/manifest.json`.
4. Build release/package outputs with:

```sh
scripts/build-release.sh
scripts/build-package-manager-runtime.sh
scripts/build-package-manager-explorer.sh
scripts/build-core-package.sh
scripts/build-source-package.sh
```

## Expected Core Artifacts

- Core: {artifacts.get("core", {}).get("sha256", "")}
- Cores info: {artifacts.get("cores_info", {}).get("sha256", "")}
- Info: {artifacts.get("info", {}).get("sha256", "")}
- Core info: {artifacts.get("core_info", {}).get("sha256", "")}
"""

with open(output_path, "w", encoding="utf-8") as fh:
    fh.write(text)
PY
}

package_version="${PACKAGE_VERSION:-$(manifest_version)}"
package_version="${package_version:-0.1.0}"
version_label="${package_version#v}"
top_dir="WASM4-Onion-source-v$version_label"
stage_parent="$out_dir/stage"
stage_root="$stage_parent/$top_dir"
archive_name="$top_dir.tar.gz"
archive_path="$out_dir/$archive_name"
manifest_path="$out_dir/source-manifest.json"

rm -rf "$stage_parent"
mkdir -p "$stage_root" "$out_dir"

for path in \
  README.md \
  LICENSE \
  build/README.md \
  build/build-wasm4-libretro-miyoo.sh \
  build/manifest.json \
  build/manifest.example.json
do
  copy_file "$path"
done

copy_tree_files docs
copy_tree_files scripts
copy_tree_files src
copy_tree_files tests
copy_tree_files package-root

write_readme_build

(
  cd "$stage_root"
  find . -type f ! -name checksums.sha256 -print0 | sort -z | xargs -0 sha256sum > checksums.sha256
)

rm -f "$archive_path" "$archive_path.sha256" "$manifest_path"
tar -C "$stage_parent" -czf "$archive_path" "$top_dir"

archive_sha="$(sha256_of "$archive_path")"
archive_size="$(size_of "$archive_path")"
printf '%s  %s\n' "$archive_sha" "$archive_name" > "$archive_path.sha256"

repo_commit="$(git -C "$repo_dir" rev-parse HEAD 2>/dev/null || printf 'unknown')"
{
  printf '{\n'
  printf '  "package_version": "%s",\n' "$(json_escape "$package_version")"
  printf '  "repository_commit": "%s",\n' "$(json_escape "$repo_commit")"
  printf '  "source_package": {\n'
  printf '    "file": "%s",\n' "$(json_escape "$archive_name")"
  printf '    "sha256": "%s",\n' "$archive_sha"
  printf '    "size_bytes": %s\n' "$archive_size"
  printf '  }\n'
  printf '}\n'
} > "$manifest_path"

rm -rf "$stage_parent"

echo "Built source package:"
echo "$archive_path"
echo "$archive_path.sha256"
echo "$manifest_path"
