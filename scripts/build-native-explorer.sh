#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build-native-explorer.sh [host|target|all] [OUT_DIR]

Builds the native WASM-4 Explorer scaffold.

Default mode: all
Default OUT_DIR: build/native-explorer

Environment:
  WASM4_TOOLCHAIN_IMAGE  Miyoo ARM32 Docker toolchain image.
  PACKAGE_VERSION        Version string compiled into the binary.
EOF
}

repo_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:-all}"
out_dir="${2:-$repo_dir/build/native-explorer}"
image="${WASM4_TOOLCHAIN_IMAGE:-aemiii91/miyoomini-toolchain:latest}"
version="${PACKAGE_VERSION:-0.1.0}"

case "$mode" in
  -h|--help)
    usage
    exit 0
    ;;
  host|target|all) ;;
  *)
    echo "Unknown build mode: $mode" >&2
    usage >&2
    exit 2
    ;;
esac

case "$out_dir" in
  /*) ;;
  *) out_dir="$PWD/$out_dir" ;;
esac

sources=(
  "$repo_dir/src/wasm4-explorer/main.c"
  "$repo_dir/src/wasm4-explorer/paths.c"
  "$repo_dir/src/wasm4-explorer/catalog.c"
  "$repo_dir/src/wasm4-explorer/thumbs.c"
  "$repo_dir/src/wasm4-explorer/install.c"
  "$repo_dir/src/wasm4-explorer/ui.c"
)

build_host() {
  mkdir -p "$out_dir/host"
  gcc \
    -std=gnu18 \
	    -Wall -Wextra -Werror \
    -O2 \
    -DW4X_VERSION="\"$version\"" \
    -o "$out_dir/host/wasm4-explorer" \
    "${sources[@]}"
}

build_target() {
  mkdir -p "$out_dir/miyoomini"
  docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$repo_dir:/repo" \
    -v "$out_dir:/out" \
    -w /repo \
    "$image" \
    /bin/bash -lc '
      set -euo pipefail
      export PATH=/opt/miyoomini-toolchain/bin:$PATH
      mkdir -p /out/miyoomini
      arm-linux-gnueabihf-gcc \
        -std=gnu18 \
	        -Wall -Wextra -Werror \
	        -O2 \
	        -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -march=armv7ve \
	        -DW4X_ENABLE_SDL \
	        -DW4X_DIRECT_FB \
	        -DW4X_VERSION="\"'"$version"'\"" \
	        -o /out/miyoomini/wasm4-explorer \
	        src/wasm4-explorer/main.c src/wasm4-explorer/paths.c src/wasm4-explorer/catalog.c \
	        src/wasm4-explorer/thumbs.c src/wasm4-explorer/install.c src/wasm4-explorer/ui.c \
	        -lSDL -lSDL_image -lSDL_ttf
      arm-linux-gnueabihf-strip --strip-unneeded /out/miyoomini/wasm4-explorer
    '
}

case "$mode" in
  host)
    build_host
    ;;
  target)
    build_target
    ;;
  all)
    build_host
    build_target
    ;;
esac

echo "Built native Explorer scaffold under $out_dir"
