#!/bin/sh
echo "$0 $*"

progdir="$(CDPATH= cd "$(dirname "$0")" && pwd)"
sdroot="$(CDPATH= cd "$progdir/../.." && pwd)"

rom="$1"
ra_dir="$sdroot/RetroArch"
ra_bin="$ra_dir/retroarch"
core_dir="$ra_dir/.retroarch/co""res"
core="$core_dir/wasm4_libretro.so"
log_dir="$sdroot/.tmp_update/logs"
log_file="$log_dir/wasm4_launch.log"
app_dir="${WASM4_EXPLORER_APP_DIR:-$sdroot/App/WASM4Explorer}"
return_marker="${WASM4_RETURN_MARKER:-$app_dir/cache/runtime/return_to_explorer}"
cmd_to_run="${WASM4_CMD_TO_RUN:-$sdroot/.tmp_update/cmd_to_run.sh}"

mkdir -p "$log_dir"

fail() {
	echo "WASM-4 launch error: $*" >&2
	echo "WASM-4 launch error: $*" >> "$log_file"
	exit 1
}

if [ -z "$rom" ]; then
	fail "missing ROM argument"
fi

if [ ! -f "$core" ]; then
	fail "missing WASM-4 core: $core"
fi

if [ ! -f "$ra_bin" ]; then
	fail "missing RetroArch binary: $ra_bin"
fi

if [ ! -f "$rom" ]; then
	fail "missing ROM: $rom"
fi

cd "$ra_dir" || fail "cannot enter RetroArch directory: $ra_dir"
HOME="$ra_dir" "$ra_bin" -v -L "$core" "$rom" >> "$log_file" 2>&1
ra_status=$?

if [ -f "$return_marker" ]; then
	marker_rom="$(sed -n '1p' "$return_marker" 2>/dev/null || true)"
	if [ "$marker_rom" = "$rom" ]; then
		rm -f "$return_marker"
		if [ -f "$cmd_to_run" ]; then
			echo "WASM-4 launch: queued Onion command exists, not returning to Explorer" >> "$log_file"
		elif [ -x "$app_dir/launch.sh" ]; then
			echo "WASM-4 launch: returning to Explorer" >> "$log_file"
			APP_DIR="$app_dir" WASM4_SD_ROOT="$sdroot" "$app_dir/launch.sh" >> "$log_file" 2>&1
		else
			echo "WASM-4 launch: missing Explorer launcher: $app_dir/launch.sh" >> "$log_file"
		fi
	fi
fi

exit "$ra_status"
