# Usage

Open `Apps -> WASM-4 Explorer`.

## Controls

- D-pad: move through the grid.
- A: install or launch the selected game.
- B: close the Explorer.
- L/R: page through the catalog.
- X: refresh the catalog.
- Menu: use Onion's GameSwitcher while a game is running.

## Catalog And Cache

Explorer downloads the catalog index on first run and reuses it on later starts. Press `X` to refresh the catalog.

Thumbnails are lazy-loaded as entries come into view. If a thumbnail is missing or stale, Explorer can fetch it the next time that entry is visible.

Games are downloaded when launched. Already installed games are reused unless the installed cart no longer matches the catalog metadata.

## Installed Games

Explorer installs games into `Roms/WASM4`, writes artwork into `Roms/WASM4/Imgs`, and updates `Roms/WASM4/miyoogamelist.xml`.

Run Onion's `Refresh ROMs` action if newly installed games do not appear under `Games -> WASM-4`.

## Updating

To update WASM-4 Explorer, download the newer release archive, copy it to the SD card root, and reinstall both Package Manager entries. Installed carts live under `Roms/WASM4` and do not need to be deleted during normal updates.

Explorer catalog and thumbnail caches live under `App/WASM4Explorer/cache`. Reopening Explorer refreshes the catalog if the cache is missing or expired; press `X` to force a refresh.

Version details and release contents are documented in [RELEASES.md](RELEASES.md).
