#!/bin/sh
set -u

usage() {
	cat <<'EOF'
Usage: update_core.sh [--force] [--manifest PATH] [--manifest-url URL] [--package-url URL]

Installs or repairs the WASM-4 core from a checked core-only package. Without
--force, the script exits without downloading when check_core.sh already passes.

Environment:
  APP_DIR                         Override /App/WASM4Explorer root.
  WASM4_SD_ROOT                   Override SD-card root.
  WASM4_CORE_RELEASE_MANIFEST     Local release-manifest.json path.
  WASM4_CORE_RELEASE_MANIFEST_URL Remote release-manifest.json URL.
  WASM4_CORE_PACKAGE_URL          Core archive URL or local path override.
  WASM4_CORE_PACKAGE_BASE_URL     Base URL used with manifest core_package.file.
EOF
}

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
app_dir="${APP_DIR:-$(CDPATH= cd -- "$script_dir/.." && pwd)}"
sd_root="${WASM4_SD_ROOT:-$(CDPATH= cd -- "$app_dir/../.." && pwd)}"
log_dir="${WASM4_LOG_DIR:-$app_dir/logs}"
cache_dir="${WASM4_CACHE_DIR:-$app_dir/cache}"
work_dir="$cache_dir/core-update"
checker="$script_dir/check_core.sh"
manifest_source="${WASM4_CORE_RELEASE_MANIFEST:-$app_dir/core-release-manifest.json}"
manifest_url="${WASM4_CORE_RELEASE_MANIFEST_URL:-}"
package_url="${WASM4_CORE_PACKAGE_URL:-}"
package_base_url="${WASM4_CORE_PACKAGE_BASE_URL:-}"
force=0

while [ "$#" -gt 0 ]; do
	case "$1" in
		--force)
			force=1
			;;
		--manifest)
			shift
			manifest_source="${1:-}"
			;;
		--manifest-url)
			shift
			manifest_url="${1:-}"
			;;
		--package-url)
			shift
			package_url="${1:-}"
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

mkdir -p "$log_dir" "$work_dir"
log_file="$log_dir/core-update.log"
manifest_path="$work_dir/release-manifest.json"
extract_dir="$work_dir/extract"
download_path="$work_dir/core-package.tar.gz"

expected_paths='RetroArch/.retroarch/core_info/wasm4_libretro.info
RetroArch/.retroarch/cores/wasm4_libretro.info
RetroArch/.retroarch/cores/wasm4_libretro.so
RetroArch/.retroarch/info/wasm4_libretro.info'

log() {
	printf '%s %s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z' 2>/dev/null || echo unknown-time)" "$*" >> "$log_file"
}

fail() {
	log "ERROR: $*"
	printf 'WASM-4 core update error: %s\n' "$*" >&2
	exit 1
}

json_value() {
	key="$1"
	sed -n 's/^[[:space:]]*"'"$key"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$manifest_path" | sed -n '1p'
}

sha256_of() {
	sha256sum "$1" | awk '{print $1}'
}

fetch_to() {
	source="$1"
	dest="$2"
	label="$3"
	tmp="$dest.tmp"

	rm -f "$tmp"
	case "$source" in
		http://*|https://*)
			command -v wget >/dev/null 2>&1 || fail "wget is required to download $label"
			wget -q -O "$tmp" "$source" || {
				rm -f "$tmp"
				fail "download failed for $label"
			}
			;;
		file://*)
			source_path="${source#file://}"
			[ -f "$source_path" ] || fail "missing $label source: $source_path"
			cp "$source_path" "$tmp" || fail "copy failed for $label"
			;;
		*)
			[ -f "$source" ] || fail "missing $label source: $source"
			cp "$source" "$tmp" || fail "copy failed for $label"
			;;
	esac
	mv "$tmp" "$dest" || fail "cannot stage $label"
}

manifest_dir() {
	case "$manifest_source" in
		file://*)
			dirname "${manifest_source#file://}"
			;;
		http://*|https://*)
			printf '\n'
			;;
		*)
			dirname "$manifest_source"
			;;
	esac
}

resolve_package_url() {
	file_name="$1"
	manifest_package_url="$(json_value url)"

	if [ -n "$package_url" ]; then
		printf '%s\n' "$package_url"
	elif [ -n "$manifest_package_url" ] && [ "$manifest_package_url" != "$file_name" ]; then
		printf '%s\n' "$manifest_package_url"
	elif [ -n "$package_base_url" ]; then
		printf '%s/%s\n' "${package_base_url%/}" "$file_name"
	else
		dir="$(manifest_dir)"
		[ -n "$dir" ] || fail "missing core package URL; set WASM4_CORE_PACKAGE_URL"
		printf '%s/%s\n' "$dir" "$file_name"
	fi
}

validate_archive_contents() {
	actual="$work_dir/archive-files.txt"
	expected="$work_dir/expected-files.txt"

	tar -tzf "$download_path" | sort > "$actual" || fail "cannot list core package archive"
	printf '%s\n' "$expected_paths" > "$expected"
	if ! cmp -s "$expected" "$actual"; then
		fail "core package contains unexpected paths"
	fi
}

check_expected_sha() {
	file_path="$1"
	expected="$2"
	label="$3"

	[ -n "$expected" ] || fail "manifest missing $label checksum"
	actual="$(sha256_of "$file_path")"
	[ "$actual" = "$expected" ] || fail "$label checksum mismatch"
}

install_payload() {
	printf '%s\n' "$expected_paths" | while IFS= read -r rel_path; do
		[ -n "$rel_path" ] || continue
		src_path="$extract_dir/$rel_path"
		dest_path="$sd_root/$rel_path"
		tmp_path="$dest_path.tmp"

		[ -f "$src_path" ] || fail "missing extracted payload: $rel_path"
		mkdir -p "$(dirname "$dest_path")"
		cp "$src_path" "$tmp_path" || fail "cannot stage $rel_path"
		chmod 0644 "$tmp_path"
		mv "$tmp_path" "$dest_path" || fail "cannot install $rel_path"
	done
}

write_core_manifest() {
	tmp="$app_dir/core-manifest.json.tmp"
	mkdir -p "$app_dir"
	{
		printf '{\n'
		printf '  "package_version": "%s",\n' "$(json_value package_version)"
		printf '  "core_sha256": "%s",\n' "$(json_value core_sha256)"
		printf '  "cores_info_sha256": "%s",\n' "$(json_value cores_info_sha256)"
		printf '  "info_sha256": "%s",\n' "$(json_value info_sha256)"
		printf '  "core_info_sha256": "%s"\n' "$(json_value core_info_sha256)"
		printf '}\n'
	} > "$tmp"
	mv "$tmp" "$app_dir/core-manifest.json" || fail "cannot update core manifest"
}

[ -x "$checker" ] || fail "missing checker: $checker"

if [ "$force" = "0" ] && "$checker" --quiet >/dev/null 2>&1; then
	log "current core passed validation; update not needed"
	printf 'WASM-4 core already valid\n'
	exit 0
fi

if [ -f "$manifest_source" ]; then
	fetch_to "$manifest_source" "$manifest_path" "core release manifest"
elif [ -n "$manifest_url" ]; then
	fetch_to "$manifest_url" "$manifest_path" "core release manifest"
else
	fail "core release manifest unavailable"
fi

archive_file="$(json_value file)"
archive_sha="$(json_value sha256)"
[ -n "$archive_file" ] || fail "manifest missing core package file"
[ -n "$archive_sha" ] || fail "manifest missing core package checksum"

resolved_package_url="$(resolve_package_url "$archive_file")"
fetch_to "$resolved_package_url" "$download_path" "core package"
check_expected_sha "$download_path" "$archive_sha" "core package"

validate_archive_contents
rm -rf "$extract_dir"
mkdir -p "$extract_dir"
tar -C "$extract_dir" -xzf "$download_path" || fail "cannot extract core package"

check_expected_sha "$extract_dir/RetroArch/.retroarch/cores/wasm4_libretro.so" "$(json_value core_sha256)" "wasm4_libretro.so"
check_expected_sha "$extract_dir/RetroArch/.retroarch/cores/wasm4_libretro.info" "$(json_value cores_info_sha256)" "cores/wasm4_libretro.info"
check_expected_sha "$extract_dir/RetroArch/.retroarch/info/wasm4_libretro.info" "$(json_value info_sha256)" "info/wasm4_libretro.info"
check_expected_sha "$extract_dir/RetroArch/.retroarch/core_info/wasm4_libretro.info" "$(json_value core_info_sha256)" "core_info/wasm4_libretro.info"

install_payload
write_core_manifest
"$checker" --quiet || fail "installed core failed validation"

log "core package installed from $resolved_package_url"
printf 'WASM-4 core updated\n'
