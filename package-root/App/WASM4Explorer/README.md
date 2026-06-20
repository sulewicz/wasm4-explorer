# WASM-4 Explorer App Package

This directory installs to `App/WASM4Explorer`.

`launch.sh` validates the WASM-4 runtime with `bin/check_core.sh`, then starts the native Explorer binary at `bin/wasm4-explorer`. Release packages include that binary; the source tree stores only the package inputs and C source.

The native Explorer downloads the live WASM-4 catalog on device, loads thumbnails lazily, installs selected carts into `Roms/WASM4`, updates WASM-4 metadata, and launches games through `/Emu/WASM4/launch.sh`.

Runtime cache and logs live under `cache/` and `logs/`. Release packages must not include generated catalogs, thumbnails, ROMs, logs, or placeholder catalog entries.
