#!/bin/sh
set -u

echo "$0 $*"

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
app_dir="${APP_DIR:-$script_dir}"
sd_root="${WASM4_SD_ROOT:-$(CDPATH= cd -- "$app_dir/../.." && pwd)}"
log_dir="${WASM4_LOG_DIR:-$app_dir/logs}"
cache_dir="${WASM4_CACHE_DIR:-$app_dir/cache}"

check_core="$script_dir/bin/check_core.sh"
native_explorer="${WASM4_NATIVE_EXPLORER_BIN:-$script_dir/bin/wasm4-explorer}"

mkdir -p "$log_dir" "$cache_dir"
log_file="$log_dir/explorer.log"

log() {
	printf '%s %s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z' 2>/dev/null || echo unknown-time)" "$*" >> "$log_file"
}

show_message() {
	title="WASM-4 Explorer"
	message="$1"

	log "$message"
	if command -v infoPanel >/dev/null 2>&1; then
		infoPanel -t "$title" -m "$message"
	else
		printf '%s: %s\n' "$title" "$message" >&2
	fi
}

fail() {
	show_message "$1"
	exit 1
}

[ -x "$check_core" ] || fail "Missing WASM-4 core checker."
[ -x "$native_explorer" ] || fail "Missing native WASM-4 Explorer binary. Reinstall the app package."

core_error="$("$check_core" --quiet 2>&1)" || {
	log "$core_error"
	fail "$(printf '%s\n' "$core_error" | sed -n '1p')"
}

log "opening native Explorer: $native_explorer"
HOME="$app_dir" "$native_explorer" --app-dir "$app_dir" --sd-root "$sd_root" "$@" >> "$log_file" 2>&1
status=$?
log "native Explorer exited with status $status"

(sleep 0.5 && echo 1 > /sys/class/pwm/pwmchip0/pwm0/enable) >/dev/null 2>&1 &

exit "$status"
