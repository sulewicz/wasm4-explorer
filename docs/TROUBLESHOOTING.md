# Troubleshooting

## Explorer Does Not Open

Install both Package Manager entries: `Emu -> WASM-4` and `Apps -> WASM-4 Explorer`.

If Explorer reports a missing native binary, reinstall `Apps -> WASM-4 Explorer` from a release archive. The source checkout does not include the compiled `bin/wasm4-explorer` file.

## Games Menu Is Missing

Install the WASM-4 runtime package, then run Onion's `Refresh ROMs`.

If the runtime package is installed but games do not launch, reinstall `Emu -> WASM-4`.

## Catalog Is Empty

Check that the device has network access, then open Explorer again or press `X` to refresh.

## Thumbnails Are Missing

Scroll to the affected games while the device is online. Thumbnails are loaded on demand and cached after download.

## A Game Does Not Appear Under Games

Open Explorer and launch the game once, then run `Refresh ROMs`.

## Logs

Relevant logs on the SD card:

- `App/WASM4Explorer/logs/explorer.log`
- `App/WASM4Explorer/logs/core.log`
- `App/WASM4Explorer/logs/core-update.log` if `bin/update_core.sh` was run
- `.tmp_update/logs/wasm4_launch.log`

Common first checks are missing runtime files, a missing native Explorer binary, and checksum mismatch messages in `core.log`.
