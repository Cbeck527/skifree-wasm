# Browser setup

Play the public mountain at **[ski.becker.foo](https://ski.becker.foo/)**.
Opening the site automatically joins multiplayer. For the original single-player
game, open [ski.becker.foo/?single=1](https://ski.becker.foo/?single=1).
See the [game controls](../README.md#game-controls) for both modes.

## Local development

Run commands from the repository root. `make serve` builds the browser and server
WASM and starts one public mountain on port 8000. Requires Node 22+ and emscripten
(both in the Nix development shell). Set `PORT` to change the listening port.
See the [development reference](development.md#node-dependencies) for the locked
npm dependencies and Nix-managed `node_modules`.

`make web` builds the frontend into `build/web/` (`index.html`, `index.js`,
`index.wasm`, `multiplayer.js`). Multiplayer needs the Node server, not just a
static host. For original single-player, open `/?single=1`; that mode also works
on a static host with all four frontend files. Browsers won't load `.wasm` from
a `file://` page.

It's the same SDL frontend, compiled with emscripten (`-sUSE_SDL=2`). The first
build downloads and compiles emscripten's SDL2 port into its cache, which under
Nix is in `/tmp`, so it happens again (with network needed) after a reboot.

## Browser behavior

- The game fills the browser window and follows it when resized, at full
  resolution on high-DPI screens. The page is `web/shell.html`.
- Single-player high scores are kept in the browser's `localStorage`
  (`SkiFree.SS`, `SkiFree.GS`, `SkiFree.FS`) and shown with `alert()`.
- Esc does nothing (a page can't minimize itself). Switching tabs or windows
  pauses single-player only; the server keeps simulating multiplayer skiers.
- Browser shortcuts (Ctrl/Cmd/Alt combinations, F5, F11 for full screen) keep
  working; the game only takes F2 and F3.

See the [multiplayer guide](multiplayer.md) for networking and shared-world
behavior, and the [deployment guide](deployment.md) for production hosting.
