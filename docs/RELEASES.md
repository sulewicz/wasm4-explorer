# Releases

WASM-4 Explorer releases are published on [GitHub Releases](https://github.com/sulewicz/wasm4-explorer/releases).

## Versioning

Release tags use `v<major>.<minor>.<patch>` versioning. The main install archive is named:

```text
WASM4-Onion-v<version>.zip
```

Builds derive the release version from `build/manifest.json` or the `PACKAGE_VERSION` environment variable. Tagged release archives use the release version in the archive name, the native Explorer binary, and `App/WASM4Explorer/core-manifest.json`.

## Archive Contents

The release archive contains generated Onion Package Manager entries:

```text
App/PackageManager/data/Emu/WASM-4 (WASM-4)
App/PackageManager/data/App/WASM-4 Explorer
```

The generated Explorer package includes the native ARM binary at:

```text
App/WASM4Explorer/bin/wasm4-explorer
```

The source checkout contains `package-root/` inputs and C source code. It is useful for development and rebuilding releases, but the release archive is the normal end-user install format.

## Build From Source

Build requirements:

- `bash`, `python3`, `sha256sum`, `tar`, and Docker.
- Access to the Miyoo Mini Docker toolchain image `aemiii91/miyoomini-toolchain:latest`.
- The checked-out WASM-4 source only if you want to rebuild `wasm4_libretro.so`.

The repository already includes the packaged WASM-4 runtime core. To build the normal install archive from the current source tree:

```sh
scripts/build-release.sh
```

The output is written to:

```text
build/release/WASM4-Onion-v<version>.zip
```

To rebuild the bundled WASM-4 runtime core first, clone the pinned WASM-4 commit from `build/manifest.json`, then run:

```sh
WASM4_DIR=/path/to/wasm4 build/build-wasm4-libretro-miyoo.sh
```

After rebuilding the core, run `scripts/build-release.sh` again.

## Updates

To update, copy the newer release archive contents to the SD card root and reinstall both Package Manager entries.

Installed games and artwork under `Roms/WASM4` are separate from the app package. Explorer catalog and thumbnail caches under `App/WASM4Explorer/cache` are reused after updates. Reopening Explorer refreshes the catalog if the cache is missing or expired; press `X` to force a refresh.

The runtime package installs the WASM-4 libretro core and core info files. Explorer validates those files on startup and reports missing or mismatched runtime files in `App/WASM4Explorer/logs/core.log`.
