# skifree-wasm

**[Play at ski.becker.foo](https://ski.becker.foo/)**

A reverse-engineered, portable C port of **SkiFree** (Chris Pirih, 1991), built from the Win32 binaries published at https://ski.ihoc.net/. It runs natively on macOS and in the browser (WebAssembly, built with emscripten).

## Quick start

```sh
nix develop        # or: direnv allow
make run           # build and play
make test          # headless checks
make serve         # public multiplayer server; play at http://localhost:8000
```

The game's sprites, strings and data tables aren't in this repo. The first build downloads `ski32.exe` from https://ski.ihoc.net/ and extracts them at build time.

More documentation: [browser setup](docs/browser.md),
[multiplayer](docs/multiplayer.md), [development reference](docs/development.md),
and [deployment](docs/deployment.md).

## Game controls

| Key | Action |
|---|---|
| Left / Right | Turn; from sideways, push off. In the air: spin |
| Down | Point downhill. In the air: reverse flip |
| Up | Climb uphill when stopped. In the air: flip |
| Space (or Insert, NumPad 0) | Hop |
| Home / End / Page Up / Page Down (fn + arrows on a Mac) | Face left / down-left / right / down-right |
| NumPad 1–9 | Direct directions, as the original's sign suggests |
| Mouse | Steer toward the cursor; click to hop or flip |
| F2 | Restart your skier |
| F3 | Pause (single-player only) |
| `f` | Fast mode (single-player only) |

Mouse steering is live whenever the cursor moves over the window, as in the original. Single-player pauses when the window loses focus; the public mountain never pauses. F2 restarts only your skier in multiplayer; F3 and fast mode are single-player only.

The three courses start below the signs at the top: Slalom (left), Freestyle (middle) and Tree Slalom (right). Ski through the start banners to begin. Keep going downhill long enough and you'll meet the Yeti.

## Status

- **Multiplayer is deployed on Fly.io**, with one shared mountain, nearby players,
  personal yeti chases and automatic reconnection. See the
  [multiplayer guide](docs/multiplayer.md) for supported behavior and limitations.
- The game logic is a complete translation, including two original bugs (see "Porting" in `analysis/ski32/NOTES.md`). It hasn't been compared frame by frame against the original running under Windows.
- No sound: the original 1.04 binary contains no sound data.
- The browser build is checked in Chrome. It hasn't been tried in Firefox or Safari, or on touch screens (SDL turns taps into mouse events, so mouse steering may work).

## Copyright

SkiFree, including its graphics and text, is © Chris Pirih. This repository contains none of the original binaries, graphics or decompiled code. The build extracts the game's data from your own download of `ski32.exe`, and `scripts/decompile.sh` generates the decompiled code from it. The browser build in `build/web/` does contain the game's graphics, extracted from that download.

The port, tools and analysis here are released under the MIT License (see `LICENSE`), which covers this project's own work only.
