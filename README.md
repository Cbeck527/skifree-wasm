# skifree-wasm

A reverse-engineered, portable C port of **SkiFree** (Chris Pirih, 1991), built from the Win32 binaries published at https://ski.ihoc.net/. It runs natively on macOS and in the browser (WebAssembly, built with emscripten).

## Quick start

```sh
nix develop        # or: direnv allow
make run           # build and play
make test          # headless checks
make serve         # public multiplayer server; play at http://localhost:8000
```

The game's sprites, strings and data tables aren't in this repo. The first build downloads `ski32.exe` from https://ski.ihoc.net/ and extracts them at build time.

## Playing

| Key | Action |
|---|---|
| Left / Right | Turn; from sideways, push off. In the air: spin |
| Down | Point downhill. In the air: reverse flip |
| Up | Climb uphill when stopped. In the air: flip |
| Space (or Insert, NumPad 0) | Hop |
| Home / End / Page Up / Page Down (fn + arrows on a Mac) | Face left / down-left / right / down-right |
| NumPad 1–9 | Direct directions, as the original's sign suggests |
| Mouse | Steer toward the cursor; click to hop or flip |
| F2 | Restart |
| F3 | Pause |
| `f` | Fast mode |

Mouse steering is live whenever the cursor moves over the window, as in the original. Single-player pauses when the window loses focus; the public mountain never pauses. F2 restarts only your skier in multiplayer; F3 and fast mode are single-player only.

The three courses start below the signs at the top: Slalom (left), Freestyle (middle) and Tree Slalom (right). Ski through the start banners to begin. Keep going downhill long enough and you'll meet the Yeti.

Options: `--size WxH` (window size in points, default 640x640) and `--tick-ms N` (40 by default; 47 matches how the original ran on Windows NT).

## In the browser

`make serve` builds the browser and server WASM and starts one public mountain on port 8000. Opening the website automatically joins it. Requires Node 22+ and emscripten (both in the Nix development shell). Set `PORT` to change the listening port.

`make web` builds the frontend into `build/web/` (`index.html`, `index.js`, `index.wasm`, `multiplayer.js`). Multiplayer needs the Node server, not just a static host. For original single-player, open `/?single=1`; that mode also works on a static host with all four frontend files. Browsers won't load `.wasm` from a `file://` page.

It's the same SDL frontend, compiled with emscripten (`-sUSE_SDL=2`). The first build downloads and compiles emscripten's SDL2 port into its cache, which under Nix is in `/tmp`, so it happens again (with network needed) after a reboot. The differences:

- The game fills the browser window and follows it when resized, at full resolution on high-DPI screens. The page is `web/shell.html`.
- High scores are kept in the browser's `localStorage` (`SkiFree.SS`, `SkiFree.GS`, `SkiFree.FS`) and shown with `alert()`.
- Esc does nothing (a page can't minimize itself). Switching tabs or windows pauses single-player only; the server keeps simulating multiplayer skiers.
- Browser shortcuts (Ctrl/Cmd/Alt combinations, F5, F11 for full screen) keep working; the game only takes F2 and F3.

## Public multiplayer (in progress)

One Node process hosts one public mountain. The server runs the shared C physics
as WASM, exposed by `src/ski_world.h`, at 25 ticks/second. Browsers send controls,
not positions, and receive authoritative snapshots over same-origin `/ws`
(WebSocket, or secure WebSocket under HTTPS). Nearby skiers have colored anonymous
labels; the HUD shows total population, distance, speed, style and completed runs.

- A seed generates one fixed mountain, including course signs, gates, trees,
  rocks, moguls, bumps and jumps. Camera size and movement do not change it.
- Up to 128 connections have independent input, motion, style points and lifecycles.
  They collide with terrain, not one another. Full-room clients retry automatically.
  Overflow is rejected with HTTP 503 before the WebSocket upgrade, including while
  admitted connections are closing; browsers show the generic reconnect message.
  This is a tested admission limit, not a production load/latency guarantee.
- Each skier gets a personal yeti chase beyond 2000 m. The chase lasts at most
  five seconds, followed by a three-second eating animation and an automatic
  respawn. F2 also resets only that skier; neither kind of reset regenerates
  the mountain or affects other players.
- Terrain is sent once on joining. Each tick sends the local skier and nearby
  players; browser rendering interpolates snapshots with a player-centered camera.
  There is no client-side prediction yet, so controls include network latency.
- Disconnects remove the skier. Reconnecting creates a new identity at the top.
  Browsers abandon silent connections after five seconds and retry after two,
  discarding stale controls rather than accumulating or replaying them.
  Heartbeats, bounded input queues, data/control-frame rate limits and slow-client removal
  keep one connection from accumulating unlimited work or buffered output.

Run `make network-test` for real HTTP/WebSocket tests (including 128 joins,
input isolation, shared terrain, interest filtering, reset/rejoin, overflow and
control-frame floods), aborted-download cleanup (Linux), shutdown with a stalled
reader, and browser transport recovery using a simulated clock/socket.
`make world-test` covers simulation contracts; `make test` includes those and
original single-player checks. `make web-test` compares WASM and native execution.
Course timing/gate results, moving NPCs, private rooms, prediction and production
load testing remain later work; multiplayer style points currently cover the
whole run rather than only the freestyle course.

For hosting, build with `make web server`, then run `npm start`. Keep one server
process for this room; multiple replicas would create separate populations.
An HTTPS reverse proxy must forward WebSocket upgrades on `/ws` as well as HTTP.
`GET /health` reports population, tick and capacity. Room state is in memory;
restarting the process removes everyone, and clients reconnect to the same seeded
terrain. Authentication, persistence and production abuse protection are not
part of this checkpoint.

## Layout

| Path | What |
|---|---|
| `etc/original-binaries/` | Not in the repo. `make` downloads the original `ski32.exe` (2005 Win32 build of 1.04) here and checks its SHA-256 |
| `analysis/ski32/` | Recovered types (`types.h`), function and global names (`symbols.tsv`), and **`NOTES.md`**, the full description of how the game works. `scripts/decompile.sh` adds the fully named and typed decompiled code, which isn't in the repo |
| `scripts/` | The decompilation pipeline (`decompile.sh` + Ghidra scripts) and helpers |
| `src/ski.c` | The game core: a function-by-function port of the decompiled code |
| `src/platform.h` | What the core needs from a frontend (clock, title, storage, message box) |
| `src/main_sdl.c` | SDL2 frontend: window, input, rendering, high scores; also the browser frontend |
| `web/shell.html` | The page around the browser build |
| `web/multiplayer.js` | Multiplayer transport, camera, interpolation and HUD |
| `server/server.mjs`, `src/server.c` | Public HTTP/WebSocket server and authoritative WASM boundary |
| `src/headless.c`, `tests/` | Headless runner (scripted input, simulated clock), test suite, fuzzer |
| `tools/gen_assets.py` | Extracts bitmaps, strings and data tables from `ski32.exe` into C at build time |

## Make targets

| Target | What |
|---|---|
| `make` | `build/port/skifree` (SDL) and `build/port/skifree-sim` (core only, no SDL) |
| `make run` | Build and play |
| `make test` | Determinism with a golden hash, the Yeti at 2000 m, all three courses finished, screenshots |
| `make world-test` | Headless shared-mountain, multi-skier, collision and respawn contracts |
| `make sanitize` | 2 × 30,000 ticks of random play under ASan + UBSan |
| `make web` | Browser build in `build/web/` |
| `make server` | Node/WASM server build and locked dependencies |
| `make serve` | Build and run the public mountain at http://localhost:8000 |
| `make network-test` | Real HTTP/WebSocket multiplayer contracts and admission limit |
| `make web-test` | The core compiled to wasm and run under node must play the same games as native: traces, high-score lists and state hashes, including 2 × 30,000 ticks of random play |
| `scripts/decompile.sh` | Re-run Ghidra and regenerate `analysis/ski32/` from `types.h` and `symbols.tsv` (run `make` first so `ski32.exe` is there) |

The headless mode is handy for experiments. For example, this skis straight down from 1950 m and saves the moment the Yeti strikes:

```sh
build/port/skifree --headless --seed 7 --ticks 70 \
  --script "0:warp=0/31200,0:down" --screenshot yeti.bmp
```

## Debugging

- `SKIFREE_SCORES=path.ini` stores high scores in that file instead of SDL's preference directory.
- `SKIFREE_SNAPSHOT=path.bmp` makes the windowed game save its window contents once, 2 seconds after starting.
- `scripts/assert_lines.py` maps the original's assert line numbers back to functions.

## Status

- The game logic is a complete translation, including two original bugs (see "Porting" in `analysis/ski32/NOTES.md`). It hasn't been compared frame by frame against the original running under Windows.
- No sound: the original 1.04 binary contains no sound data.
- The browser build is checked in Chrome. It hasn't been tried in Firefox or Safari, or on touch screens (SDL turns taps into mouse events, so mouse steering may work).

## Copyright

SkiFree, including its graphics and text, is © Chris Pirih. This repository contains none of the original binaries, graphics or decompiled code. The build extracts the game's data from your own download of `ski32.exe`, and `scripts/decompile.sh` generates the decompiled code from it. The browser build in `build/web/` does contain the game's graphics, extracted from that download.

The port, tools and analysis here are released under the MIT License (see `LICENSE`), which covers this project's own work only.
