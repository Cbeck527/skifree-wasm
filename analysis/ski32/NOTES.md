# ski32.exe — reverse-engineering notes

Working notes on SkiFree 1.04 (Win32 build). Everything here comes from the
Ghidra export in this directory; regenerate it with `scripts/decompile.sh`.

## Files

| File | What | Regenerated? |
|---|---|---|
| `symbols.tsv` | Hand-maintained names/signatures/comments. **Edit this**, then rerun. | no — source of truth |
| `decompiled-game.c` | Decompiled game code only (0x401000–0x406cc1) | yes |
| `decompiled.c` | Everything, including the C runtime | yes |
| `functions.tsv` | Every function: size, region (game/crt), callers, callees | yes |
| `imports.tsv` | Imported Win32 functions and which game functions call them | yes |
| `strings.tsv` | Strings in the binary and which functions use them | yes |
| `resources/bitmaps/` | 89 sprite bitmaps (`wrestool -x --type=2`) | `wrestool` |
| `resources/sprite-sheet.png` | All sprites on one sheet, labelled by resource id | `scripts/sprite_sheet.py` |

Pipeline: `ghidra-analyzeHeadless` → `CreateMissingFunctions.java` (finds
pointer-only functions such as window/timer procs) → `ApplySymbols.java`
(applies `symbols.tsv`) → `ExportAnalysis.java`. The Ghidra project is saved in
`build/ghidra/` and can be opened in the GUI (`ghidra`).

## The binary

- Stored in `etc/original-binaries/` with the two rebuilds, all downloaded from https://ski.ihoc.net/.
- PE32 i386 GUI, linked 2005-10-02 with MSVC 6 (linker 6.0). The title card says "Version 1.04".
- Not packed. Sections: `.text`, `.rdata`, `.data`, `.rsrc`.
- Imports 96 functions: KERNEL32, USER32, GDI32, and `sndPlaySoundA` from WINMM.
- The game is one source file, `V:\hack\ski32\ski2.c` (from `assert` messages), roughly 4,400+ lines long.
- **`ski32-rebuild-vs6.exe` is the same code.** Toolchain, Rich header, `.rdata` and `.rsrc` are identical. Every `.text` difference is a `.data` address shifted by +0x30, because the assert path string is longer (`F:\Documents and Settings\Administrator\Desktop\ski32-project\ski2.c`). Useful as a cross-check, but it doesn't need separate analysis.
- `ski32-rebuild-vs2019.exe` is 3.4× larger, mostly modern CRT. It's harder to read; ignore it unless something here is ambiguous.

### Game vs C runtime

| Range | Contents |
|---|---|
| 0x401000–0x406cc1 | Game code, 108 functions, about 22 KB. Functions are 16-byte aligned. |
| 0x406cd0–end | Statically linked MSVC 6 CRT: `srand`/`rand` first, then `WinMainCRTStartup` at 0x406d83, heap, string and locale code. |

Ghidra's function-signature (FID) databases recognise only a few CRT functions (`malloc`, `strlen`, `memset`, …). The rest of the CRT isn't worth naming; a port replaces it wholesale.

**Calling convention:** game functions are `__fastcall` (first two arguments in ECX/EDX). `ski2.c` was probably compiled with `/Gr`. Windows callbacks are `__stdcall`.

## Architecture

```
WinMain
  AllocGlobals        LocalAlloc: string cache, sprite table, actor pool
  InitGameState       srand(GetTickCount()), actor free list, tick = 40 ms
  InitInstance        RegisterClass "SkiMain" + "SkiStatus", CreateWindow, load sounds
  NewGame             player = NewActor(type 0, state 3); BuildCourses; StartGameTimer
  GetMessage loop

SetTimer(hwndMain, 666, 40ms, GameTimerProc)   → 25 ticks/second
GameTimerProc → GameTick (only while the game is active)
  UpdateWorld
    UpdateActor(a)      for each actor (dispatch on type)
    cull actors that left the screen
    update course objects / special entities
    HandleCollision(a, b) for each overlapping pair
    SpawnAtEdge(dir)    for every 60 px scrolled in each direction
    1-in-666 chance per tick → NewActor(3, 0x1f), a new snowboarder
  DrawActors(hdc, dirtyRect)   redraw only changed rects (BitBlt)
  DrawStatusPanel      every ~327 ms: Time / Dist / Speed / Style

MainWndProc
  WM_CREATE           LoadBitmaps        WM_DESTROY   FreeBitmaps
  WM_SIZE / WM_ACTIVATE                  WM_PAINT     OnPaint
  WM_GETMINMAXINFO    min size 320×300
  WM_KEYDOWN          OnKeyDown          WM_CHAR      OnChar
  WM_MOUSEMOVE        OnMouseMove        WM_LBUTTONDOWN/DBLCLK  OnMouseClick
```

Key facts for a port:
- The simulation runs at a fixed 40 ms tick, driven by a Windows timer rather than a busy loop. That maps directly onto `requestAnimationFrame` plus an accumulator.
- Drawing is dirty-rectangle BitBlt of 4-bit bitmaps onto a white background. A port can just redraw everything each frame.
- Randomness is the MSVC CRT `rand()` LCG, seeded from `GetTickCount()`. Reimplement it exactly to reproduce runs.
- High scores live in `entpack.ini`, section `[Ski]` (shared with the Windows Entertainment Pack). Use `localStorage` in a browser.

## Actors

There's a fixed pool of 100 actors (`g_actorPool`), each 0x50 bytes, kept on two singly linked lists: active (`g_actorList`) and free (`g_actorFreeList`).

Partial struct layout (offsets confirmed in code; the "probably" names need checking):

| Offset | Type | Field |
|---|---|---|
| 0x00 | ptr | next actor in list |
| 0x04 | ptr | linked actor (redrawn together) |
| 0x08 | int | ? (cleared when drawn) |
| 0x0c | int | ? `UpdateWorld` only calls `UpdateActor` when this is 0; probably marks actors driven by a course list |
| 0x10 | u16 | sprite id (= bitmap resource id) |
| 0x14 | ptr | `&g_sprites[sprite id]` (16-byte sprite records) |
| 0x18 | int | type (0–0x11, asserted by the constructors) |
| 0x1c | int | state (0–63; indexes `g_stateSprite`) |
| 0x20 | RECT | cached screen rect (valid when flag 0x04 is set) |
| 0x30 | RECT | rect last drawn |
| 0x40 | s16 | x |
| 0x42 | s16 | y (distance downhill; Dist = y/16 m) |
| 0x44 | s16 | z (height, for jumps) |
| 0x46 | s16 | probably dx |
| 0x48 | s16 | probably forward speed (the status panel's Speed uses it) |
| 0x4a | s16 | probably dz |
| 0x4c | u32 | flags: 0x01, 0x02 (being removed), 0x04 (rect cached), 0x08, 0x10 (needs redraw), 0x20 |

### Types and states

The state numbers are global; each type owns a range. `SetActorState` looks up the sprite in `g_stateSprite` (0x40a1ac).

Two constructors: `NewActor(type, state)` for animated actors and `NewActorWithSprite(type, spriteId)` for static ones. `SpawnAtEdge` picks between them using a per-type initial-state table at 0x40a22c.

| Type | Update function | States | Sprites | Object |
|---|---|---|---|---|
| 0 | inline in `UpdateActor` | 0x00–0x15 | 1–22 | Player skier. Physics per pose in a table at 0x40a308 (16-byte rows). |
| 1 | `UpdateNpcSkier` | 0x16–0x1a | 28–32 | Computer-controlled skier |
| 2 | `UpdateDog` | 0x1b–0x1e | 33–36 | Dog ("WOOF!", sound slot 8) |
| 3 | `UpdateSnowboarder` | 0x1f–0x26 | 37–44 | Snowboarder |
| 4–8 | via the course list at 0x40c720 (not `UpdateActor`) | 0x27–0x37 | 65–81 | Chairlift chairs (type 4: state 0x27 going one way with dz −2, 0x29 the other with dz +2, one every 0x800 in y) and the Yeti (type 7 starts in state 0x2a = sprite 68). Created by `BuildCourses`. Not traced further. |
| 9 | `UpdateBurningTree` | 0x38–0x3b | 83,84,85,84 | Burning tree, as an animation loop |
| 10 | `UpdateWalkingTree` | 0x3c–0x3f | 49, 87–89 | Tree that starts walking |
| 0x0b–0x11 | never (`UpdateWorld` filters `type < 0xb`) | — | any | Static scenery and course objects (trees, rocks, flags, signs, lift towers). Type 0x11 is also used for what the dog leaves behind (sprite 82). |


## Sprites (bitmap resource ids)

See `resources/sprite-sheet.png`. All are 4-bit, 16-colour bitmaps.

| Ids | What |
|---|---|
| 1–22 | Player skier: directions, crash ("OUCH!"), sitting, jumps and tricks |
| 23–26 | Slalom flags (red/blue arrows) and poles (green/red) |
| 27 | Mogul / snow mound (64×32) |
| 28–32 | Computer-controlled skier, including crashed |
| 33–36 | Dog, including "WOOF!" |
| 37–44 | Snowboarder |
| 45–48 | Rock, stump, small bumps |
| 49–51 | Tree, dead tree, big tree |
| 52 | Rainbow jump ramp |
| 53–56 | Title card ("SkiFree Copyright 1991 by Chris Pirih"), "Version 1.04", "Use NumPad (0-9)", "F2 = Restart F3 = Pause" |
| 57–60 | Start / Finish banners (left and right) |
| 61–63 | Course signs: Slalom, Tree Slalom, Freestyle |
| 64–67 | Chairlift tower and chairs |
| 68–81 | Yeti, including eating the skier |
| 82 | What the dog leaves behind (created by `UpdateDog` as a type 0x11 static actor) |
| 86 | Small item, not identified yet |
| 83–85 | Burning tree |
| 87–89 | Walking-tree frames |

## Strings

Most UI text is in the string table (`GetResString(id)` caches `LoadStringA`):

```
1 SkiFree          2 Ski Paused ... Press F3 to continue
3 Time:  4 Dist:  5 Speed:  6 Style:
7 00:00:00.00  8 " 0000m"  9 " 0000m/s"  10 0000000
11 %2u:%2.2u:%2.2u.%2.2u  12 %5.2dm  13 %5.2dm/s  14 %7ld
15 High Scores  16 " <-- that's you!"  17 " <-- try again!"
```

Other strings live in `.data`: window class names `SkiMain` and `SkiStatus`, icon `iconSki`, the `nosound` switch, and "Whoa, like, can't load bitmaps! Yer outa memory, duuude!".

## Sound: dead in this build

`InitInstance` tries to load nine `WAVE` resources (ids 1–9) and `PlaySoundSlot` plays them with `sndPlaySound(ptr, SND_ASYNC|SND_MEMORY)`. **`ski32.exe` contains no WAVE resources**, so every slot is null and nothing plays. A port can leave sound out, or add new sounds at the existing `PlaySoundSlot` call sites. Known slots: 1, 4 (player events), 8 (dog). The rest aren't traced yet.

## Assert line numbers

45 game functions call `AssertFailed(file, line)`. Their line numbers give a rough map back to `ski2.c` (for example `UpdateActor` asserts at lines 2311–2335). Two caveats:
- **Line ranges are not function boundaries.** Small helpers are inlined. `UpdateActor` (584 bytes) contains asserts from lines 2022–2335 because the player's update logic is inlined into it.
- Function order in the binary only mostly follows source order: 30 of the 45 functions are in ascending line order.

Print the full map with `scripts/assert_lines.py`.

## Next steps

1. **Struct recovery.** Define `Actor` (0x50), `Sprite` (16 bytes) and the per-pose physics row (16 bytes at 0x40a308) as Ghidra data types. Teach `ApplySymbols.java` to load them from a header file, and type the actor parameters, so the decompiled code shows `a->x` instead of `*(short *)(param_1 + 0x40)`.
2. Name the remaining 57 game functions. Start with `HandleCollision` (1,492 bytes), `OnKeyDown`, `SpawnAtEdge`, `BuildCourses`, and the Yeti/chairlift path (states 0x27–0x37).
3. Name the game globals (`DAT_0040c5xx–c9xx`): view scroll offsets, timers, score, course state.
