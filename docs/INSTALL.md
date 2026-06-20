# Installation

## Requirements

- Miyoo Mini-class OnionOS device.
- Network access for catalog refreshes, thumbnails, and cart downloads.
- SD card mounted on your computer.
- WASM-4 Onion release archive from [GitHub Releases](https://github.com/sulewicz/wasm4-explorer/releases), or a release archive you build from source.

The app was tested on OnionOS hardware using the bundled ARM32 WASM-4 libretro runtime. Installed games can be launched offline after their cart files are already on the SD card.

## Steps

1. Download `WASM4-Onion-v<version>.zip` from the latest GitHub Release.
2. Extract the release archive on your computer.
3. Confirm the extracted archive contains these generated Package Manager entries:
   - `App/PackageManager/data/Emu/WASM-4 (WASM-4)`
   - `App/PackageManager/data/App/WASM-4 Explorer`
4. Copy the extracted folders to the SD card root.
5. Safely eject the SD card and boot OnionOS.
6. Open `Apps -> Package Manager`.
7. Install `Emu -> WASM-4`.
8. Install `Apps -> WASM-4 Explorer`.
9. Open `Apps -> WASM-4 Explorer`.

On first run, Explorer downloads the WASM-4 gallery index. Thumbnails are fetched while browsing and carts are downloaded only when launched.

The source tree's `package-root/` directory is not a complete SD-card install by itself. Release archives include generated Package Manager metadata and the compiled native binary at `App/WASM4Explorer/bin/wasm4-explorer`.

## Build From Source

To build the install archive yourself instead of downloading a GitHub Release, follow [Build From Source](RELEASES.md#build-from-source), then install the generated `build/release/WASM4-Onion-v<version>.zip` using the same steps above.

## Games Menu

Installed games are written to `Roms/WASM4`. To show them in Onion's Games tab, run `Refresh ROMs`, then open `Games -> WASM-4`.
