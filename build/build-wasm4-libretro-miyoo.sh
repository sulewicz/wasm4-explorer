#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
wasm4_dir="${WASM4_DIR:-}"
image="${WASM4_TOOLCHAIN_IMAGE:-aemiii91/miyoomini-toolchain:latest}"

build_dir="$repo_dir/build/wasm4-libretro-miyoo"
log_dir="$repo_dir/build/logs"
log_file="$log_dir/wasm4_libretro_miyoo_onion_build.log"
core_out="$repo_dir/package-root/RetroArch/.retroarch/cores/wasm4_libretro.so"
cores_info_out="$repo_dir/package-root/RetroArch/.retroarch/cores/wasm4_libretro.info"
info_out="$repo_dir/package-root/RetroArch/.retroarch/info/wasm4_libretro.info"
core_info_out="$repo_dir/package-root/RetroArch/.retroarch/core_info/wasm4_libretro.info"

mkdir -p "$log_dir" "$(dirname "$core_out")" "$(dirname "$cores_info_out")" "$(dirname "$info_out")" "$(dirname "$core_info_out")"

if [ -z "$wasm4_dir" ]; then
  echo "Set WASM4_DIR to a local checkout of the pinned WASM-4 source." >&2
  exit 2
fi

if [ ! -d "$wasm4_dir/.git" ]; then
  echo "Missing wasm4 checkout: $wasm4_dir" >&2
  exit 1
fi

git -C "$wasm4_dir" submodule update --init runtimes/native/vendor/wasm3

docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$wasm4_dir:/src" \
  -v "$repo_dir:/repo" \
  -w /src/runtimes/native \
  "$image" \
  /bin/bash -lc '
    set -euxo pipefail
    export PATH=/opt/miyoomini-toolchain/bin:$PATH
    rm -rf /repo/build/wasm4-libretro-miyoo
    cmake -B /repo/build/wasm4-libretro-miyoo \
      -DCMAKE_BUILD_TYPE=Release \
      -DLIBRETRO=ON \
      -DWASM_BACKEND=wasm3 \
      -DCMAKE_SYSTEM_NAME=Linux \
      -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc \
      -DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++ \
      -DCMAKE_C_FLAGS="-fomit-frame-pointer -ffast-math -march=armv5te -mtune=arm926ej-s -ftree-vectorize -funswitch-loops" \
      -DCMAKE_CXX_FLAGS="-fomit-frame-pointer -ffast-math -march=armv5te -mtune=arm926ej-s -ftree-vectorize -funswitch-loops"
    cmake --build /repo/build/wasm4-libretro-miyoo --target wasm4_libretro -- -j"$(nproc)"
    arm-linux-gnueabihf-strip --strip-unneeded /repo/build/wasm4-libretro-miyoo/wasm4_libretro.so
    cp /repo/build/wasm4-libretro-miyoo/wasm4_libretro.so /repo/package-root/RetroArch/.retroarch/cores/wasm4_libretro.so
    file /repo/package-root/RetroArch/.retroarch/cores/wasm4_libretro.so
    readelf -h /repo/package-root/RetroArch/.retroarch/cores/wasm4_libretro.so | sed -n "1,28p"
  ' 2>&1 | tee "$log_file"

curl -LfsS \
  https://raw.githubusercontent.com/libretro/libretro-core-info/master/wasm4_libretro.info \
  -o "$info_out"
cp "$info_out" "$cores_info_out"
cp "$info_out" "$core_info_out"

{
  echo
  echo "Artifact checksums:"
  sha256sum "$core_out" "$cores_info_out" "$info_out" "$core_info_out"
} | tee -a "$log_file"

echo "Built $core_out"
