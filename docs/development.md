# Development reference

Run commands from the repository root, inside `nix develop` unless noted
otherwise. See the [quick start](../README.md#quick-start) to build and play,
and [browser setup](browser.md) for the WASM frontend.

## Node dependencies

`nix develop` builds the locked npm dependencies (including `ws`) in the Nix
store and links them into `node_modules`. To add a Node dependency, use
`npm install --package-lock-only <package>` and re-enter the shell; don't run
`npm install` or `npm ci` over the Nix-managed links. Outside Nix, `make serve`
installs dependencies with `npm ci` as before.

Both Nix shells and `make server` check that direct dependencies actually resolve
to the locked store directories. If an older npm-managed `node_modules` conflicts,
move that directory aside and re-enter `nix develop`; the check never deletes it.

## Repository layout

| Path | What |
|---|---|
| `etc/original-binaries/` | Not in the repo. `make` downloads the original `ski32.exe` (2005 Win32 build of 1.04) here and checks its SHA-256 |
| `analysis/ski32/` | Recovered types (`types.h`), function and global names (`symbols.tsv`), and [the reverse-engineering notes](../analysis/ski32/NOTES.md), the full description of how the game works. `scripts/decompile.sh` adds the fully named and typed decompiled code, which isn't in the repo |
| `scripts/` | The decompilation pipeline (`decompile.sh` + Ghidra scripts) and helpers |
| `src/ski.c` | The game core: a function-by-function port of the decompiled code |
| `src/platform.h` | What the core needs from a frontend (clock, title, storage, message box) |
| `src/main_sdl.c` | SDL2 frontend: window, input, rendering, high scores; also the browser frontend |
| `web/shell.html` | The page around the browser build |
| `web/multiplayer.js` | Multiplayer transport, camera, interpolation and HUD |
| `server/server.mjs`, `src/server.c` | Public HTTP/WebSocket server and authoritative WASM boundary |
| `src/headless.c`, `tests/` | Headless runner (scripted input, simulated clock), test suite, fuzzer |
| `tools/gen_assets.py` | Extracts bitmaps, strings and data tables from `ski32.exe` into C at build time |
| `docs/` | Browser setup, multiplayer behavior, development reference and deployment guide |
| `www/` | Brochure site for https://cmb.software/skifree-wasm/ (Astro on Cloudflare; see [its README](../www/README.md)) |

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

## Debugging and headless runs

Options: `--size WxH` (window size in points, default 640x640) and `--tick-ms N`
(40 by default; 47 matches how the original ran on Windows NT).

The headless mode is handy for experiments. For example, this skis straight down
from 1950 m and saves the moment the Yeti strikes:

```sh
build/port/skifree --headless --seed 7 --ticks 70 \
  --script "0:warp=0/31200,0:down" --screenshot yeti.bmp
```

- `SKIFREE_SCORES=path.ini` stores high scores in that file instead of SDL's preference directory.
- `SKIFREE_SNAPSHOT=path.bmp` makes the windowed game save its window contents once, 2 seconds after starting.
- `scripts/assert_lines.py` maps the original's assert line numbers back to functions.

See the [multiplayer guide](multiplayer.md) for server behavior and test coverage,
and the [deployment guide](deployment.md) for Fly configuration and validation.
