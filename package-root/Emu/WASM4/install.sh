#!/bin/sh

romroot="/mnt/SDCARD/Emu/WASM4/../../Roms/WASM4"
ra_root="/mnt/SDCARD/RetroArch/.retroarch"

mkdir -p "$romroot" "$romroot/Imgs" "$romroot/.wasm4"
mkdir -p "$ra_root/cores/cache"

rm -f \
	"$ra_root/cores/cache/core_extensions.cache" \
	"$ra_root/cores/cache/ext_cores_wasm.cache"

if [ -f /mnt/SDCARD/.tmp_update/script/build_ext_cache.sh ]; then
	sh /mnt/SDCARD/.tmp_update/script/build_ext_cache.sh "$ra_root"
fi

if [ -f /mnt/SDCARD/.tmp_update/script/reset_list.sh ]; then
	sh /mnt/SDCARD/.tmp_update/script/reset_list.sh "$romroot" 2>/dev/null || true
fi

rm -f "$romroot/WASM4_cache2.db" "$romroot/WASM4_cache6.db"

sync
