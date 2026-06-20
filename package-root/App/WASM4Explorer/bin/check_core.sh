#!/bin/sh
set -u

usage() {
	cat <<'EOF'
Usage: check_core.sh [--quiet] [--manifest PATH]

Validates the installed WASM-4 libretro core and info files.

Environment:
  APP_DIR                     Override /App/WASM4Explorer root.
  WASM4_SD_ROOT               Override SD-card root.
  WASM4_CORE_PATH             Override wasm4_libretro.so path.
  WASM4_CORE_INFO_PATH        Override cores/wasm4_libretro.info path.
  WASM4_INFO_PATH             Override info/wasm4_libretro.info path.
  WASM4_CORE_INFO_COPY_PATH   Override core_info/wasm4_libretro.info path.
  WASM4_CORE_MANIFEST         Override manifest path.
EOF
}

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
app_dir="${APP_DIR:-$(CDPATH= cd -- "$script_dir/.." && pwd)}"
sd_root="${WASM4_SD_ROOT:-$(CDPATH= cd -- "$app_dir/../.." && pwd)}"
manifest="${WASM4_CORE_MANIFEST:-$app_dir/core-manifest.json}"
quiet=0

while [ "$#" -gt 0 ]; do
	case "$1" in
		--quiet)
			quiet=1
			;;
		--manifest)
			shift
			manifest="${1:-}"
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Unknown argument: $1" >&2
			usage >&2
			exit 2
			;;
	esac
	shift
done

core_path="${WASM4_CORE_PATH:-$sd_root/RetroArch/.retroarch/cores/wasm4_libretro.so}"
cores_info_path="${WASM4_CORE_INFO_PATH:-$sd_root/RetroArch/.retroarch/cores/wasm4_libretro.info}"
info_path="${WASM4_INFO_PATH:-$sd_root/RetroArch/.retroarch/info/wasm4_libretro.info}"
core_info_copy_path="${WASM4_CORE_INFO_COPY_PATH:-$sd_root/RetroArch/.retroarch/core_info/wasm4_libretro.info}"
log_dir="${WASM4_LOG_DIR:-$app_dir/logs}"
mkdir -p "$log_dir"
log_file="$log_dir/core.log"
errors=0

log() {
	printf '%s %s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z' 2>/dev/null || echo unknown-time)" "$*" >> "$log_file"
}

report_error() {
	errors=1
	log "ERROR: $*"
	printf 'WASM-4 core check: %s\n' "$*" >&2
}

json_value() {
	key="$1"
	[ -f "$manifest" ] || return 0
	sed -n 's/^[[:space:]]*"'"$key"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$manifest" | sed -n '1p'
}

check_required_file() {
	path="$1"
	label="$2"

	if [ ! -f "$path" ]; then
		report_error "missing $label at $path. Install the WASM-4 runtime package from Onion Package Manager."
	fi
}

sha256_of() {
	sha256sum "$1" | awk '{print $1}'
}

check_sha256() {
	path="$1"
	expected="$2"
	label="$3"

	[ -n "$expected" ] || return 0
	[ -f "$path" ] || return 0

	if ! command -v sha256sum >/dev/null 2>&1; then
		report_error "cannot validate $label checksum because sha256sum is unavailable."
		return 0
	fi

	actual="$(sha256_of "$path")"
	if [ "$actual" != "$expected" ]; then
		report_error "$label checksum mismatch. Reinstall the WASM-4 runtime package."
		log "expected $expected for $path"
		log "actual   $actual for $path"
	fi
}

check_required_file "$core_path" "wasm4_libretro.so"
check_required_file "$cores_info_path" "wasm4_libretro.info"
check_required_file "$info_path" "RetroArch info/wasm4_libretro.info"
check_required_file "$core_info_copy_path" "RetroArch core_info/wasm4_libretro.info"

if [ -f "$manifest" ]; then
	check_sha256 "$core_path" "$(json_value core_sha256)" "wasm4_libretro.so"
	check_sha256 "$cores_info_path" "$(json_value cores_info_sha256)" "cores/wasm4_libretro.info"
	check_sha256 "$info_path" "$(json_value info_sha256)" "info/wasm4_libretro.info"
	check_sha256 "$core_info_copy_path" "$(json_value core_info_sha256)" "core_info/wasm4_libretro.info"
else
	log "manifest not present, skipping checksum validation: $manifest"
fi

if [ "$errors" -ne 0 ]; then
	exit 1
fi

log "core validation passed"
if [ "$quiet" = "0" ]; then
	printf 'WASM-4 core OK\n'
fi
