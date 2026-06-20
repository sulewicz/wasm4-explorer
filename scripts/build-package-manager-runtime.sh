#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build-package-manager-runtime.sh [OUT_DIR]

Builds an Onion Package Manager-compatible WASM-4 runtime package from
package-root/. The generated output is ignored by git and can be copied to an
OnionOS SD card root for installation through Package Manager.

Default OUT_DIR: build/package-manager-runtime
EOF
}

repo_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="${1:-$repo_dir/build/package-manager-runtime}"
package_name="WASM-4 (WASM-4)"
package_dir="$out_dir/App/PackageManager/data/Emu/$package_name"
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

require_file "Emu/WASM4/config.json"
require_file "Emu/WASM4/launch.sh"
require_file "Emu/WASM4/install.sh"
require_file "Icons/Default/wasm4.png"
require_file "RetroArch/.retroarch/cores/wasm4_libretro.so"
require_file "RetroArch/.retroarch/cores/wasm4_libretro.info"
require_file "RetroArch/.retroarch/info/wasm4_libretro.info"
require_file "RetroArch/.retroarch/core_info/wasm4_libretro.info"
require_file "Saves/CurrentProfile/config/WASM-4/WASM-4.opt"

rm -rf "$package_dir"
mkdir -p \
  "$package_dir/Emu" \
  "$package_dir/Icons/Default" \
  "$package_dir/Roms/WASM4/Imgs" \
  "$package_dir/Roms/WASM4/.wasm4" \
  "$package_dir/RetroArch/.retroarch/cores" \
  "$package_dir/RetroArch/.retroarch/info" \
  "$package_dir/RetroArch/.retroarch/core_info" \
  "$package_dir/Saves/CurrentProfile/config/WASM-4"

cp -a "$src/Emu/WASM4" "$package_dir/Emu/"
cp -a "$src/Icons/Default/wasm4.png" "$package_dir/Icons/Default/"
cp -a "$src/RetroArch/.retroarch/cores/wasm4_libretro.so" "$package_dir/RetroArch/.retroarch/cores/"
cp -a "$src/RetroArch/.retroarch/cores/wasm4_libretro.info" "$package_dir/RetroArch/.retroarch/cores/"
cp -a "$src/RetroArch/.retroarch/info/wasm4_libretro.info" "$package_dir/RetroArch/.retroarch/info/"
cp -a "$src/RetroArch/.retroarch/core_info/wasm4_libretro.info" "$package_dir/RetroArch/.retroarch/core_info/"
cp -a "$src/Saves/CurrentProfile/config/WASM-4/WASM-4.opt" "$package_dir/Saves/CurrentProfile/config/WASM-4/"

: > "$package_dir/Roms/WASM4/.gitkeep"
: > "$package_dir/Roms/WASM4/Imgs/.gitkeep"
: > "$package_dir/Roms/WASM4/.wasm4/.gitkeep"

chmod 755 "$package_dir/Emu/WASM4/launch.sh" "$package_dir/Emu/WASM4/install.sh"

if find "$package_dir/Roms/WASM4" -type f \( -name '*.wasm' -o -name 'WASM4_cache*.db' \) | grep -q .; then
  echo "Generated package contains test ROMs or generated ROM cache databases" >&2
  exit 1
fi

find "$package_dir" -type f | sort > "$out_dir/manifest.files"

echo "Built Package Manager runtime package:"
echo "$package_dir"
