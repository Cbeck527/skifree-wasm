# ski32.exe — reverse-engineering notes

How SkiFree 1.04 (Win32) works, recovered from `etc/original-binaries/ski32.exe`.
The decompiled code in this directory uses the names and types described
here. Regenerate it with `scripts/decompile.sh`.

## Files

| File | What | Regenerated? |
|---|---|---|
| `types.h` | Recovered structs and enums (`Actor`, `Placement`, `Sprite`, `Motion`, …). **Edit this.** | no — source of truth |
| `symbols.tsv` | Names, signatures and comments for every game function and global. **Edit this.** | no — source of truth |
| `decompiled-game.c` | Decompiled game code only (0x401000–0x406cc1), fully named and typed | yes |
| `decompiled.c` | Everything, including the C runtime | yes |
| `functions.tsv` | Every function: size, region (game/crt), callers, callees | yes |
| `imports.tsv` | Imported Win32 functions and which game functions call them | yes |
| `strings.tsv` | Strings in the binary and which functions use them | yes |
| `resources/bitmaps/` | 89 sprite bitmaps (`wrestool -x --type=2`) | `wrestool` |
| `resources/sprite-sheet.png` | All sprites on one sheet, labelled by resource id | `scripts/sprite_sheet.py` |

Pipeline: `ghidra-analyzeHeadless` → `CreateMissingFunctions.java` (finds functions reached only through pointers, such as window and timer procedures) → `ApplyTypes.java` (parses `types.h`) → `ApplySymbols.java` (applies `symbols.tsv`) → `ExportAnalysis.java`. The Ghidra project is saved in `build/ghidra/` and opens in the GUI (`ghidra`).

All 108 game functions and all game globals are named. Most names come from reading the code; the sound-slot names (`g_sndCrash` etc.) describe the event that plays them, since the WAVE files themselves are absent.

## The binary

- Stored in `etc/original-binaries/` with the two rebuilds, all downloaded from https://ski.ihoc.net/.
- PE32 i386 GUI, linked 2005-10-02 with MSVC 6 (linker 6.0). The title card says "Version 1.04".
- Not packed. Sections: `.text`, `.rdata`, `.data`, `.rsrc`.
- Imports 96 functions: KERNEL32, USER32, GDI32, and `sndPlaySoundA` from WINMM.
- The game is one source file, `V:\hack\ski32\ski2.c` (from `assert` messages), roughly 4,400+ lines long.
- **`ski32-rebuild-vs6.exe` is the same code.** Toolchain, Rich header, `.rdata` and `.rsrc` are identical. Every `.text` difference is a `.data` address shifted by +0x30, because the assert path string is longer (`F:\Documents and Settings\Administrator\Desktop\ski32-project\ski2.c`).
- `ski32-rebuild-vs2019.exe` is 3.4× larger, mostly modern CRT. Ignore it unless something here is ambiguous.

| Range | Contents |
|---|---|
| 0x401000–0x406cc1 | Game code, 108 functions, about 22 KB. 16-byte aligned. |
| 0x406cd0–end | Statically linked MSVC 6 CRT: `srand`, `rand`, `atol`, `WinMainCRTStartup` (0x406d83), heap, locale. Not worth naming further. |

**Calling convention:** game functions are `__fastcall` (first two args in ECX/EDX; `ski2.c` was probably built with `/Gr`). Windows callbacks are `__stdcall`. `symbols.tsv` signatures must respect this.

## Architecture

```
WinMain
  AllocGlobals        string cache, 90 Sprites, 100 Actors, 256 Placements
  InitGameState       srand(GetTickCount()), empty lists, 40 ms tick
  InitInstance        single-instance check, RegisterClass SkiMain + SkiStatus, CreateWindow
    WM_CREATE → LoadBitmaps → LoadSpriteStrips   pack bitmaps into image/mask strips
  NewGame             player = NewActor(AT_PLAYER, PS_LEFT) at (0,0); PlaceTitleSigns;
                      BuildCourses; StartGameTimer
  GetMessage loop

SetTimer(hwnd, 666, 40 ms, GameTimerProc) → GameTick while g_gameActive
  GameTick
    UpdateWorld
      for free-roaming actors with type < AT_MOGUL: UpdateActor
      cull actors outside g_spawnRect (client rect + 120 px)
      UpdatePlacementList ×4 (courses, lift towers), UpdateMovingPlacements (chairs, yetis)
      FreeDeletedActors
      HandleCollision(a, b) and (b, a) for overlapping pairs where one has AF_MOVED
      SpawnAtEdge(edge) for every 60 px scrolled in each direction
      1 in 666: a snowboarder at the top edge
    DrawActors(g_hdcMain, &g_clientRect)
    DrawStatusPanel every ~327 ms
```

Notes for a port:
- **Simulation is per tick, not per millisecond.** Every speed is in pixels per 40 ms tick. Only the Speed readout uses real elapsed time. Run a fixed 25 Hz update and render as often as you like.
- **Rendering** is dirty-rectangle BitBlt with masks, plus "ghost" actors that erase stale images (`SpawnEraseGhost`). A port can redraw everything each frame and drop ghosts, the `drawNext` grouping, `AF_DRAWN/GHOST/REDRAW` and the cached rects. Keep the draw order: `DrawActor` draws each overlap group back to front by `y` (flat sprites use `y - height`, so they go under everything). One catch: `ActorOrGhost` uses the ghost as an actor's previous position for collision crossings, so a port needs to remember last tick's `y` instead.
- **Coordinates:** world pixels, +y downhill. `ComputeSpriteRect` puts a sprite centred on `x` with its bottom at `y - z`. The view actor is drawn at the screen anchor: horizontal centre, one third down (`OnResize`/`SetScreenAnchor`).
- **Randomness:** MSVC `rand()` (`seed = seed*0x343fd + 0x269ec3; (seed>>16) & 0x7fff`), seeded from `GetTickCount()`. `Random(n)` is `rand() % n`.
- **High scores:** top 10 per course in `entpack.ini`, section `[Ski]`, keys `SS` (Slalom), `GS` (Tree Slalom), `FS` (Freestyle). Times are stored negated so all three sort descending. A browser port would use `localStorage`.

## Data structures

See `types.h` for the full definitions with offsets.

- **`Actor`** (0x50 bytes; pool of 100): list links, sprite, `type`, `state`, position `x, y, z`, velocity `dx, dy, dz`, `flags`, and a back-pointer to the `Placement` it came from. Two lists: active (`g_actorList`) and free (`g_actorFreeList`). `g_player` is the skier; `g_viewActor` is what the camera follows. They're the same actor until the Yeti eats the skier.
- **`Placement`** (0x24 bytes; 256 in `g_placements`): a pre-laid-out object with its own position and velocity. It spawns an `Actor` while near the view, and the actor is despawned when it leaves. Courses, lift towers, chairs and yetis are placements, grouped in `PlacementList`s sorted by `y`.
- **`Sprite`** (16 bytes; `g_sprites[resource id]`): DCs for the image and mask strips, y offset in the strip, width, height, and area (used as a density budget).
- **`Motion`** (16 bytes): one row per state, in three tables: `g_playerMotion`, `g_skierMotion` and `g_snowboarderMotion`. Each tick `ApplyMotion` moves `dy` toward `dyMax` by `dyAccel` (or down by 2 if above it), and `|dx|` toward `dxRatio*dy/2` by `dxAccel` (down by 2 if above), in direction `dxDir`.
- **Tables:** `g_stateSprite[64]` (state → sprite), `g_initialState[11]` (spawn state per type), `g_turnTable[22]` (player left/right), `g_landingState[9]` (airborne state → state on touchdown).

### Types and states

| Type | Behaviour | States → sprites |
|---|---|---|
| `AT_PLAYER` 0 | inline in `UpdateActor` | 0–21 → 1–22 |
| `AT_SKIER` 1 | `UpdateSkier`: 1/12 per tick picks down/left/right | 22–26 → 28–32 |
| `AT_DOG` 2 | `UpdateDog`: walks, woofs; 1/100 leaves sprite 82 | 27–30 → 33–36 |
| `AT_SNOWBOARDER` 3 | `UpdateSnowboarder`: 1/10 switches left/right | 31–38 → 37–44 |
| `AT_CHAIRLIFT` 4 | placement, `UpdateChairlift` | 39–41 → 65–67 |
| `AT_YETI_*` 5–8 | placement, `UpdateYeti` | 42–55 → 68–81 |
| `AT_BURNING_TREE` 9 | `UpdateBurningTree`: animation loop | 56–59 → 83,84,85,84 |
| `AT_WALKING_TREE` 10 | `UpdateWalkingTree`: looks like a tree; 1/100 starts walking | 60–63 → 49, 87–89 |
| `AT_MOGUL` … `AT_DECOR` 11–17 | static scenery | sprite chosen directly |

Player states: `PS_DOWN` 0, `PS_DOWN_LEFT` 1, `PS_LEFT_DOWN` 2, `PS_LEFT` 3, `PS_DOWN_RIGHT` 4, `PS_RIGHT_DOWN` 5, `PS_RIGHT` 6, walk 7–8, climb 9–10, `PS_CRASHED` 11, `PS_SITTING` 12, airborne 13–21. The motion table gives straight down a top speed of 16 px/tick, the diagonals 12 and 6, sideways 0, and airborne states 20–24.

## Gameplay rules

### Controls (`OnKeyDown`, `OnChar`, mouse)

| Input | On the ground | In the air |
|---|---|---|
| Left / Right, NumPad 4/6 | Turn one step (`g_turnTable`). From sideways, push off (`dx` −8 / +8) | Spin: 13 → 14 → 16 → 15 → 13 |
| Down, NumPad 2 | `PS_DOWN` | Reverse flip: 13 → 19 → 18 → 13; from 20/21 back to 14/15 |
| Up, NumPad 8 | When stopped sideways or sitting: climb uphill (`dy = -4`) | Flip: 13 → 18 → 19 → 13; from 14/15 → 20/21 |
| Home/End/PgUp/PgDn, NumPad 7/1/9/3 | `PS_LEFT` / `PS_DOWN_LEFT` / `PS_RIGHT` / `PS_DOWN_RIGHT` | — |
| Insert, NumPad 0 | Hop (`dz = 2`), and `dy -= 4` if fast | — |
| Mouse move | Steer toward the cursor (`DirStateFromMouse`) | Spin pose by quadrant (`AirStateFromMouse`) |
| Click | Hop (`dz = 4`) | Flip |
| F2 | Restart | |
| F3 | Pause / resume | |
| Esc | Minimize | |
| Enter or click, after being eaten | Restart | |
| `f` | Toggle fast mode (everything moves twice as far per tick) | |
| `x` `X` `y` `Y` / `r` / `t` | Debug: nudge the player ±2 px / redraw / run one tick | |

Each tick in `UpdateActor`, the walk and climb states (7–10) fall back to standing sideways, so walking is one push per key press. After a crash the skier slides to a stop (`dx`, `dy` decay by 1 per tick), then sits; turning gets up.

### Landing

When `z` reaches 0 in an airborne state, `g_landingState` decides: `PS_JUMP` → `PS_DOWN`, `PS_JUMP_LEFT` → `PS_LEFT`, `PS_JUMP_RIGHT` → `PS_RIGHT`, and anything else (mid-spin facing back, tumbling, mid-flip) → `PS_CRASHED`.

**Apparent original bug:** after the lookup, `UpdateActor` compares the result with 17 (`PS_TUMBLE`) to apply −64 style and the crash sound. The table never contains 17 (a crash landing gives 11), so that branch is dead and every landing plays the "land" sound with no penalty. A port should decide deliberately whether to keep this.

### Collisions (`HandleCollision(a, b)`)

Collisions use screen-rect overlap. Most outcomes also require a **crossing**: `a` and `b` swapped vertical order this tick (or are level), using each actor's ghost as its previous position. "Above" something means `a->z` is greater than `b`'s top (`b->sprite->height + b->z`).

For the player, by what was hit:

| Hit | Effect |
|---|---|
| Dog, flag, sign/decor | On crossing: `dy /= 2`. Dog mess (sprite 82): −16 style |
| Mogul | If heading straight down: little hop (`dz = 1`), `dy /= 2` if fast |
| Bump | On the ground: hop (`dz = 4`), +1 style. In the air below its top: bounce (`dz = dy/2`) |
| Jump ramp | Crossing low and moving downhill: launch (`dz = dy`), `PS_JUMP`, +1 style |
| Rock/stump, in the air | Clearing item 86: collect it, +100 style. Crossing it low: bounce like a bump |
| Tree, rock/stump on the ground, skier, snowboarder, chair, burning or walking tree | Jumping over or passing under: +6 style (burning tree: +1000, and it becomes a dead tree). Otherwise, on crossing: glancing a tree (horizontal offset more than half the wider sprite) gives `dy /= 2`. Anything else is a crash: `PS_CRASHED` on the ground, `PS_TUMBLE` in the air, −32 style. Two exceptions skip the penalty: tumbling into a dead tree sets it on fire (+16 style), and backing into a stump (`dy < 0`) turns it into item 86 (you still fall) |

Other actors:

| Actor | Effect |
|---|---|
| Skier | Knocked over by anything (`SK_CRASHED`, or `SK_CRASHED_AIR` if the hitter was airborne); +20 style if it was the player |
| Dog | Woofs when something moving hits it; +3 style if the player |
| Snowboarder | Bumps and ramps make it jump; hitting the player (+20 style), a skier, a snowboarder, a tree or a rock below its top makes it tumble |
| Yeti | Touching the player: the player is deleted and the Yeti eats for about 3 s, then stands |
| Walking tree | Stops walking |

Style points only count while on the Freestyle course (`AddStyle`). In addition, `UpdateActor` adds −1 per tick while walking or climbing, +2 per tick mid-spin facing back, +4 per tick in a flip, and +8 per tick in a side flip.

### Courses (`BuildCourses`, `Check*`)

All three start at y = 640. Each one starts when the player crosses that line inside its start window. A run is cancelled by going back above y = 640. Crossings are timed to sub-tick precision by interpolating (`InterpolateCrossing`).

| Course | Start window (x) | Finish y | Score | Gates |
|---|---|---|---|---|
| Slalom | −576 … −320 | 8,640 | time | 24 flags every 320 from y = 960, alternating sprite 23 at x = −496 and 24 at x = −400 |
| Tree Slalom | 320 … 512 | 16,640 | time | 39 flags every 400 from y = 1040, alternating 23 at x = 400 and 24 at x = 432 |
| Freestyle | −160 … 160 | 16,640 | style points | none |

A gate with sprite 23 (left arrow) must be passed on its left, and 24 (right arrow) on its right. Passed gates turn into sprite 25; missed ones turn into 26 and add 5 seconds. During a run the status panel's Dist counts down to the finish.

`BuildCourses` also places the title signs, 13 lift towers (x = −128, every 2048 from y = −1024 to 23552), the chairs, and four yetis. The Tree Slalom loop also computes a random tree placement each gate but never adds it, though it still consumes two `rand()` values. Keep those calls if you want runs to reproduce exactly.

### Chairlift and Yetis

- Chairs go uphill at x = −112 (`dy = -2`, height 32, carrying a snowboarder) and downhill at x = −144 (`dy = +2`, empty). They turn around at y = −1024 and y = 23552. A loaded chair has a 1/1000 chance per tick of a snowboarder jumping off.
- Four yetis start standing far out:

  | Yeti | Starts at | Wakes when the player passes |
  |---|---|---|
  | Downhill | (0, 32060) | y > 32000 (2,000 m: the famous one) |
  | Uphill | (0, −2060) | y < −2000 |
  | Left | (−16060, 0) | x < −16000 |
  | Right | (16060, 0) | x > 16000 |

  Once awake, a Yeti chases at up to 16 px/tick sideways and 26 px/tick down (10 up). If it falls more than a screen behind, it jumps to a screen away, so it can't be outrun. While idle it drifts back past its line, and now and then it jumps.

### Spawning

Every 60 px of scrolling, one object spawns just beyond the leading edge. The mix depends on which course zone the spawn position falls in. Nothing spawns if the summed area of live sprites (`g_onScreenArea`) exceeds a fraction of the spawn rect's area.

| Zone | Density cap | Mix |
|---|---|---|
| Open slope | area / 32 | 45% tree, 20% bump, 20% rock/stump, 5% mogul, 5% walking tree, 2% jump, 2% skier, 1% dog |
| Slalom (x −576…−320, y 640…8640) | area / 64 | moguls only |
| Tree Slalom (x 320…512, y 640…16640) | area / 16 | trees; 1/64 dog |
| Freestyle (x −160…160, y 640…16640) | area / 32 | 30% bump, 20% rock, 20% jump, 18% tree, 10% mogul, 2% walking tree |

Sprite variants: trees are 6/8 normal (49), 1/8 dead (50) and 1/8 big (51); rocks are 3/4 rock (45) and 1/4 stump (46); bumps are 2/3 small (47) and 1/3 larger (48).

## Sprites (bitmap resource ids)

See `resources/sprite-sheet.png`. All are 4-bit, 16-colour bitmaps.

| Ids | What |
|---|---|
| 1–22 | Player: 7 directions, walk, climb, crash ("OUCH!"), sitting, jump, spin, tumble, flips |
| 23–26 | Slalom flags (left/right arrows) and judged poles (passed green / missed red) |
| 27 | Mogul |
| 28–32 | Skier, including crashed |
| 33–36 | Dog, including "WOOF!" |
| 37–44 | Snowboarder, including the crash tumble |
| 45–48 | Rock, stump, small bumps |
| 49–51 | Tree, dead tree, big tree |
| 52 | Rainbow jump ramp |
| 53–56 | Title card, "Version 1.04", "Use NumPad (0-9)", "F2 = Restart F3 = Pause" |
| 57–60 | Start / Finish banners (left and right) |
| 61–63 | Course signs: Slalom, Tree Slalom, Freestyle |
| 64–67 | Chairlift tower; chairs (loaded going up, empty going up, going down) |
| 68–81 | Yeti: stand, jump, run left/right/up, eating the skier |
| 82 | Dog mess |
| 83–85 | Burning tree |
| 86 | Item a stump turns into when backed into; +100 style for jumping over it |
| 87–89 | Walking tree |

## Strings

The string table (`GetResString(id)`):

```
1 SkiFree          2 Ski Paused ... Press F3 to continue
3 Time:  4 Dist:  5 Speed:  6 Style:
7 00:00:00.00  8 " 0000m"  9 " 0000m/s"  10 0000000
11 %2u:%2.2u:%2.2u.%2.2u  12 %5.2dm  13 %5.2dm/s  14 %7ld
15 High Scores  16 " <-- that's you!"  17 " <-- try again!"
```

`.data` holds the window class names `SkiMain` and `SkiStatus`, icon `iconSki`, the `nosound` switch, the INI names, and "Whoa, like, can't load bitmaps! Yer outa memory, duuude!".

## Sound: dead in this build

`InitInstance` loads nine `WAVE` resources into `Sound` slots, and `PlaySoundSlot` plays them with `sndPlaySound(data, SND_ASYNC|SND_MEMORY)`. **`ski32.exe` has no WAVE resources**, so every slot is empty and nothing plays. The call sites tell you what each sound was for: crash (1), jump (2), dog hit (3), land (4), snowboarder jump (5), skier knocked over (6), yeti eats you (7), dog mess (8), yeti chasing (9). A port can add new sounds there.

## Assert line numbers

45 game functions call `AssertFailed(file, line)`; `scripts/assert_lines.py` prints the line range per function. Two caveats:
- **Line ranges are not function boundaries.** Small helpers are inlined. `UpdateActor` (584 bytes) contains asserts from lines 2022–2335 because the player update is inlined into it.
- Function order mostly follows source order: 30 of the 45 functions are in ascending line order.

## Next steps

- Tidy the remaining raw spots in the decompiled output: `(int)a->type < 0xb` comparisons, and the stack temporary in `BuildCourses` that is really a `Placement`.
- Port: write the game logic as portable C against a small platform layer (draw sprite, input events, a timer, storage), using `decompiled-game.c` and this file as the spec. Build it for the browser with emscripten, and natively on macOS (for example with SDL) for side-by-side testing.
