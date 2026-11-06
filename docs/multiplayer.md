# Public multiplayer

**The public mountain is live at [ski.becker.foo](https://ski.becker.foo/).**
Opening the site joins one shared mountain hosted by the Fly.io app
`skifree-multiplayer`: one always-on Machine in US East (`iad`), with one shared
vCPU and 512 MiB RAM. Releases are manual and reset the room. See the
[deployment guide](deployment.md) for configuration, domain setup and operations.

## Shared-world behavior

One Node process hosts the mountain. The server runs the shared C physics as
WASM, exposed by `src/ski_world.h`, at 25 ticks/second. Browsers send controls,
not positions, and receive authoritative snapshots over same-origin `/ws`
(secure WebSocket under HTTPS). Nearby skiers have colored anonymous labels;
the HUD shows total population, distance, speed, style and completed runs.

- A seed generates one fixed mountain, including course signs, gates, trees,
  rocks, moguls, bumps and jumps. Camera size and movement do not change it.
- Up to 128 connections have independent input, motion, style points and
  lifecycles. They collide with terrain, not one another. Full-room clients
  retry automatically. Overflow is rejected with HTTP 503 before the WebSocket
  upgrade, including while admitted connections are closing; browsers show the
  generic reconnect message. This is a tested admission limit, not a production
  load/latency guarantee.
- Each skier gets a personal yeti chase beyond 2000 m. The chase lasts at most
  five seconds, followed by a three-second eating animation and an automatic
  respawn. F2 also resets only that skier; neither kind of reset regenerates
  the mountain or affects other players.
- Terrain is sent once on joining. Each tick sends the local skier and nearby
  players; browser rendering interpolates snapshots with a player-centered
  camera. There is no client-side prediction, so controls include network latency.
- Disconnects remove the skier. Reconnecting creates a new identity at the top.
  Browsers abandon silent connections after five seconds and retry after two,
  discarding stale controls rather than accumulating or replaying them.
  Heartbeats, bounded input queues, data/control-frame rate limits and slow-client
  removal keep one connection from accumulating unlimited work or buffered output.

F3 and fast mode are single-player only; the shared mountain never pauses.
For the original game, use [single-player mode](https://ski.becker.foo/?single=1).

## Hosting

From the repository root, build with `make web server`, then run `npm start`.
Keep one server process for this room; multiple replicas would create separate
populations. An HTTPS reverse proxy must forward WebSocket upgrades on `/ws`
as well as HTTP. `GET /health` reports population, tick and capacity.

Room state is in memory. Restarting or deploying removes everyone, and clients
reconnect to the same seeded terrain. The Fly deployment intentionally keeps
exactly one Machine and disables autostop. See the [deployment guide](deployment.md)
for release and rollback procedures, including the stopped Railway deployment.

The brochure page at [cmb.software/skifree-wasm](https://cmb.software/skifree-wasm/)
is a separate static site in `www/`; it links to the game rather than hosting
another mountain. See [its README](../www/README.md).

## Tests and limitations

Run `make network-test` for real HTTP/WebSocket tests (including 128 joins,
input isolation, shared terrain, interest filtering, reset/rejoin, overflow and
control-frame floods), aborted-download cleanup (Linux), shutdown with a stalled
reader, and browser transport recovery using a simulated clock/socket.
`make world-test` covers simulation contracts; `make test` includes those and
original single-player checks. `make web-test` compares WASM and native execution.

Multiplayer is deployed, but it does not yet include course timing/gate results,
moving NPCs, private rooms or client-side prediction. Style points cover the
whole run rather than only the freestyle course. There is no player
authentication or persistent world/player state. Existing connection limits
are not complete production abuse protection.

Sustained-load benchmarks, spend alerts and external uptime monitoring remain
to be set up. The 128-connection admission cap is not a measured capacity or
latency guarantee.
