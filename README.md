# skifree-wasm

A reverse-engineered, portable C port of **SkiFree** (Chris Pirih, 1991), built from the Win32 binaries published at https://ski.ihoc.net/. It runs natively on macOS today, and is structured so the same code can be built for the browser with emscripten.

## Quick start

```sh
nix develop        # or: direnv allow
make run           # build and play
make test          # headless checks
```

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

Mouse steering is live whenever the cursor moves over the window, as in the original. The game pauses when the window loses focus.

The three courses start below the signs at the top: Slalom (left), Freestyle (middle) and Tree Slalom (right). Ski through the start banners to begin. Keep going downhill long enough and you'll meet the Yeti.

Options: `--size WxH` (window size in points, default 640x640) and `--tick-ms N` (40 by default; 47 matches how the original ran on Windows NT).

## Layout

| Path | What |
|---|---|
| `etc/original-binaries/` | The original `ski32.exe` (2005 Win32 build of 1.04) plus the VS6 and VS2019 rebuilds |
| `analysis/ski32/` | Ghidra output: fully named and typed decompiled game code, recovered types (`types.h`), symbols, sprites, and **`NOTES.md`**, the full description of how the game works |
| `scripts/` | The decompilation pipeline (`decompile.sh` + Ghidra scripts) and helpers |
| `src/ski.c` | The game core: a function-by-function port of the decompiled code |
| `src/platform.h` | What the core needs from a frontend (clock, title, storage, message box) |
| `src/main_sdl.c` | SDL2 frontend: window, input, rendering, high scores |
| `src/headless.c`, `tests/` | Headless runner (scripted input, simulated clock), test suite, fuzzer |
| `tools/gen_assets.py` | Extracts bitmaps, strings and data tables from `ski32.exe` into C at build time |

## Make targets

| Target | What |
|---|---|
| `make` | `build/port/skifree` (SDL) and `build/port/skifree-sim` (core only, no SDL) |
| `make run` | Build and play |
| `make test` | Determinism with a golden hash, the Yeti at 2000 m, all three courses finished, screenshots |
| `make sanitize` | 2 × 30,000 ticks of random play under ASan + UBSan |
| `scripts/decompile.sh` | Re-run Ghidra and regenerate `analysis/ski32/` from `types.h` and `symbols.tsv` |

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
- Next: a browser build with emscripten.
