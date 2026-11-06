# Fly.io deployment

Status: **deployed to Fly.io** at https://ski.becker.foo. First deployed on
2026-11-05, in organization `personal`. Release `v1` runs image
`registry.fly.io/skifree-multiplayer:deployment-01M6PX0KVDSZW5Y521HMQMKFR1` on
one Machine, `8911153a23d1e8`, in `iad` with one shared vCPU and 512 MiB RAM.
Public shared IPv4 and dedicated IPv6 are allocated; HTTPS and the service health
check pass. No application secrets, volumes or CI autodeploy were configured.

The first release passed static JS/WASM delivery, advancing ticks, reciprocal
secure WebSocket visibility, movement/reset/disconnect/rejoin, two independent
browser sessions and the original `?single=1` fallback. The former Railway
deployment is stopped and its old endpoint returns HTTP 404. Its project and
service remain intact, as described below.

## Runtime contract

| Setting | Configuration |
| --- | --- |
| Builder | Root multi-stage `Dockerfile`, locked Nix build shell, `make web server` |
| Runtime | Non-root Node 24; direct `node server/server.mjs` command |
| App / environment | One production app for testing and public play; manual releases |
| Region | `iad` (Ashburn, Virginia) |
| Machine count | **Exactly one**, including stopped Machines; first deploy needs `--ha=false` |
| Machine size | One shared vCPU / 512 MiB RAM; starting allocation, not load-tested capacity |
| Idle behavior | Autostop disabled; autostart enabled for a stopped Machine |
| Networking | Fly terminates HTTPS, redirects HTTP, forwards to `0.0.0.0:8000` |
| Environment | `NODE_ENV=production`, `PORT=8000`; matches `http_service.internal_port` |
| Health check | `GET /health`, every 15 seconds, 2-second timeout, 10-second startup grace |
| Shutdown | `SIGTERM`, 5-second host timeout; application bounds HTTP drain to 4 seconds |
| Restart policy | On failure, up to 10 retries |
| Deployment | Rolling, one unavailable Machine allowed; brief downtime and room reset |
| Database / volumes / app secrets | None |

One Node process serves the website, same-origin `/ws` upgrades and the C/WASM
world at 25 ticks/second. The browser automatically uses `wss:` under HTTPS;
there is no separate backend URL, CORS setup or frontend hostname to change.

**One Machine is a correctness constraint, not just a cost choice.** Every process
creates a separate mountain. `primary_region` chooses placement for new Machines;
it does not restrict existing regions. Disabling autostop keeps the simulation
running with no players; it does not set a replica count. `--ha=false` prevents
automatic spare creation, but does not remove existing extra Machines. Inspect
all Machines, including stopped ones: autostart can bring an extra room online.
Do not add replicas, regions, process groups, canary or blue/green deployments.
Sticky sessions would not merge the worlds. Scale resources vertically only
after measuring CPU, memory, tick rate and outbound bandwidth.

The server enforces its existing 128-player admission cap. No proxy concurrency
hard limit is set: 128 WebSockets must not block health checks or static files.
This admission cap is not a production throughput guarantee.

## Custom domain

Cloudflare hosts these DNS-only records in the `becker.foo` zone, both with
a 300-second TTL:

| Type | Name | Target |
| --- | --- | --- |
| CNAME | `ski.becker.foo` | `n915g2d.skifree-multiplayer.fly.dev` |
| CNAME | `_acme-challenge.ski.becker.foo` | `ski.becker.foo.n915g2d.flydns.net` |

Fly manages the Let's Encrypt certificate and terminates HTTPS and secure
WebSockets directly; Cloudflare's HTTP proxy is disabled for these records.
Keep the ACME challenge record for certificate renewal. The targets came from
`flyctl certs setup ski.becker.foo --app skifree-multiplayer`; do not infer them
from the public app hostname. Check issuance and validation with
`flyctl certs check ski.becker.foo --app skifree-multiplayer`.

The default https://skifree-multiplayer.fly.dev hostname remains available and
serves the same mountain. Custom-domain setup required no redeploy or restart.

## Build contract and local checks

The existing Dockerfile builds both WASM bundles with pinned toolchains, installs
locked production npm dependencies separately, and copies only Node, `ws`, the
server, license and generated bundles into the non-root runtime image:

- `build/web/{index.html,index.js,index.wasm,multiplayer.js}`
- `build/server/{core.cjs,core.wasm}`

The first build downloads `ski32.exe` from its author's site and checks its
SHA-256 before extracting assets. A download outage fails the build; it should
not replace the running release. `.dockerignore` allowlists build inputs, keeping
local builds, Nix-store symlinks, `.git`, `.amp` and credential files out of the
remote build context. Do not include deployment tokens in the image or build args.

Run the credential-free checks first:

```sh
nix develop
python3 -c "import pathlib, tomllib; tomllib.loads(pathlib.Path('fly.toml').read_text()); print('fly.toml syntax OK')"
make test web-test network-test
docker build -t skifree:fly .
docker run --rm --read-only --tmpfs /tmp --tmpfs /app/web:uid=1000,gid=1000 \
  --mount type=bind,source="$(pwd)/tests/server.test.mjs",target=/app/tests/server.test.mjs,readonly \
  skifree:fly sh -c 'cp build/web/multiplayer.js web/multiplayer.js && exec node --test tests/server.test.mjs'
docker run -d --name skifree-check -e PORT=8123 -p 127.0.0.1:8123:8123 skifree:fly
curl --fail http://127.0.0.1:8123/health
docker exec skifree-check id -u  # 1000, not root
docker stop --time 5 skifree-check
docker inspect --format '{{.State.ExitCode}}' skifree-check  # 0, not 137
docker rm skifree-check
```

The image test uses `/tmp` for disposable test fixtures and copies the **shipped**
browser transport into a temporary `/app/web` mount, where the existing tests
expect to read it. No development JavaScript or dependencies replace image files.

Docker needs a local engine; it is not supplied by the Nix shell. The Python
check validates **TOML syntax only**, not Fly's schema, app availability or account
permissions. The full development shell provides the Fly CLI version pinned by
`flake.lock`; the smaller Docker build shell does not need the deployment CLI.
No Nix lock update is required for this migration.

The graphics remain Chris Pirih's copyrighted material; see the root README.
The MIT license covers this project's own work, not the extracted game assets.

## Operator workflow (requires authorization)

These commands are instructions for a later release, **not local validation**.
App creation, uploads, Machine changes and Railway shutdown change shared/paid
resources. Do not run them merely to check this configuration. Even
`flyctl launch --no-deploy` creates resources; `flyctl deploy --build-only` is not
a credential-free local build. Pass `--app` explicitly: the `FLY_APP` environment
variable can otherwise override the app name in `fly.toml`.

1. Complete local verification. The production app `skifree-multiplayer` already
   exists in organization `personal`; do not create another app for a redeploy.
   Inspect ownership and target before changing resources. A separate staging
   app, if added later, would be a separate mountain with a globally unique name.
2. Authenticate with `flyctl auth login` (or an appropriately scoped deployment
   token supplied as `FLY_API_TOKEN` from the operator's secret store). The running
   game needs no token. Never commit credentials or put them in Docker build args.
3. Validate the local configuration against Fly and inspect all Machines:

   ```sh
   flyctl config validate --strict --app skifree-multiplayer --config fly.toml
   flyctl machine list --app skifree-multiplayer
   ```

   `fly:check` runs `flyctl config validate --strict` against the Fly platform;
   it requires authentication and is not an offline evaluator. Confirm there is
   exactly one Machine total, only in `iad`, before proceeding. The first release
   provisioned this app with `flyctl apps create skifree-multiplayer --org personal`,
   preserving the hand-written configuration instead of using `flyctl launch`.
4. After authorizing the release, run from the repository root:

   ```sh
   flyctl deploy --app skifree-multiplayer --config fly.toml --ha=false --remote-only --depot=false
   flyctl machine list --app skifree-multiplayer
   flyctl status --app skifree-multiplayer
   flyctl checks list --app skifree-multiplayer
   ```

   Use `--ha=false` on every release, especially the first: Fly otherwise defaults
   to redundant Machines for HTTP services. The first deploy allocates public
   IPs for the HTTP service. Verify exactly one Machine total in `iad`, healthy
   and running, with the configured CPU and memory. Stop if extra Machines exist;
   removing them is a separate resource change, not something validation fixes.
   `--depot=false` selects Fly's remote builder; separate builder resources do not
   host a game room. Do not detach the deploy: wait for health and smoke checks.
5. Run the acceptance checks below on `https://ski.becker.foo` after
   every release. The former Railway deployment remains stopped.
   Set budget alerts and uptime monitoring; neither is created by `fly.toml`.

## Cutover and rollback

Railway's `public-mountain-production.up.railway.app` hostname belongs to Railway
and cannot be moved to Fly. Production now uses https://ski.becker.foo with
Fly-managed TLS and Cloudflare DNS, as described above. The README links to this
address; the old Railway endpoint returns HTTP 404, not a redirect to Fly.

Fly has passed the initial HTTP/WebSocket/browser checks and custom-domain
HTTPS/WebSocket checks. With explicit authorization, Railway deployment
`776ea26a-fc93-4e6d-8c9d-b0bdbff23b81` was stopped using `deploymentRemove`,
the same operation as `railway down`. Verification showed `REMOVED`,
`deploymentStopped=true`, and no active or pending deployments. There were no
connected players, repository/image sources, repository triggers or cron jobs.

The Railway project `96e84ee4-92c2-483f-bc85-cc2ee3ee0db3`, environment
`production` (`fd31421c-73f9-458f-b6bf-bf731c50df59`), and service `public-mountain`
(`ecadbfbf-a548-482f-8525-b6aad55aa154`) are retained. They were not deleted or
reconfigured. Fly's original Machine and advancing simulation were unchanged.
The old Railway release reports `canRedeploy=false`; restoring Railway requires
a fresh deployment, not a one-click redeploy of that release. Its earlier source
and IaC configuration remain in Git history. Deleting the retained Railway
resources requires separate authorization.

No world/player state transfers between providers. Players must open the new
address; browsers left on the old hostname do not discover Fly automatically.
A rollback restores the previous image or production address, not player state.
To revert a Fly release, redeploy a known-good image with
`flyctl deploy --app skifree-multiplayer --config fly.toml --ha=false --image <previous-image-reference>` after authorization;
keep the singleton configuration and verify health afterward. Re-enabling Railway
and switching the address back also requires authorization and resets the room.

Rolling updates stop and replace the sole Machine rather than booting a second
world. Expect downtime even on successful releases. If the new version fails
startup or health checks, the old process is not guaranteed to remain serving:
inspect logs and explicitly restore the known-good image. Do not assume Railway's
old-startup-failure behavior carries over to Fly.

## Acceptance and operations

- `/health` returns HTTP 200, capacity 128, and an advancing tick across requests.
  Fly's service check gates routing but only checks HTTP status; it does not
  verify tick progress or automatically restart an unhealthy process.
- `/`, `/index.js`, `/index.wasm` and `/multiplayer.js` load, with WASM served as
  `application/wasm`; `/?single=1` still runs original single-player.
- Two independent browsers connect with `wss://<app-name>.fly.dev/ws`, share the
  population/terrain and see each other moving. Reset affects only that skier;
  disconnect/rejoin removes the old skier and creates a new identity.
- Verify the yeti/eating/respawn cycle. An authorized restart/redeploy disconnects
  players and lets clients reconnect to the replacement room without ghost skiers.
- Inspect `flyctl logs`, health checks and Machine count after every release.
  SIGTERM terminates WebSockets and bounds stalled HTTP readers to four seconds;
  Fly's five-second stop timeout is best-effort, not a guaranteed drain window.
- Measure sustained load before broad traffic; monitor CPU, memory, tick rate,
  connection count and outbound bandwidth. Configure failure and spend alerts.
  Existing per-connection limits are not complete public abuse protection.

References: [Fly configuration](https://fly.io/docs/reference/configuration/),
[deployments](https://fly.io/docs/launch/deploy/),
[redundancy defaults](https://fly.io/docs/apps/app-availability/),
and [health checks](https://fly.io/docs/monitoring/checks/).
