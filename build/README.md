# Build And Release Metadata

This directory contains the public metadata needed to rebuild the release artifacts.

The current packaged runtime is documented in [manifest.json](manifest.json). Release metadata records:

- package version,
- this repository commit,
- upstream `wasm4` commit,
- OnionOS/RetroArch validation baseline,
- libretro core info source,
- selected WASM backend,
- Miyoo ARM32 toolchain image or version,
- SHA256 checksums for bundled core and info files,
- license file paths included in the package and source/build asset.

Use [manifest.example.json](manifest.example.json) as the starting shape.

## Rebuild The WASM-4 Core

The repository already includes the runtime core used by the current package. To rebuild it yourself, clone the pinned WASM-4 source commit from `manifest.json`, then run:

```sh
WASM4_DIR=/path/to/wasm4 build/build-wasm4-libretro-miyoo.sh
```

The script uses the Onion Miyoo Mini Docker toolchain image and writes the rebuilt core to `package-root/RetroArch/.retroarch/cores/wasm4_libretro.so`.

## Build The Install Archive

```sh
scripts/build-release.sh
```

The install archive is written to `build/release/WASM4-Onion-v<version>.zip`. Generated files under `build/` are ignored except for this metadata and build helper.
