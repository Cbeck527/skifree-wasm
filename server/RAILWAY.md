# Railway deployment specification

Status: **deployed to production** at
https://public-mountain-production.up.railway.app.
The root `Dockerfile` builds the game and server, `.dockerignore`
restricts build inputs, and `.railway/railway.ts` defines the service settings.
The Railway CLI and locked development SDK are available through `nix develop`.
No account, credentials, project ID, domain or region is baked into the image.

The initial release passed HTTP/static-WASM delivery, real secure WebSocket
join/movement/reset/disconnect/rejoin checks, and two independent browser sessions
sharing the mountain. The original `?single=1` fallback also loads successfully.
Resource limits, sustained-load benchmarks, spend alerts and external uptime
monitoring are not configured yet; the resource target below is only a proposal.

## Project shape

```text
Browser ── HTTPS / WSS ── Railway public domain
                              │
                              ▼
                    public-mountain service
                    one Node process / replica
                    ├── browser files + /health
                    ├── /ws connections
                    └── C/WASM world, 25 ticks/sec
```

| Setting | Initial specification |
| --- | --- |
| Project name | `skifree-multiplayer` |
| Project ID | `96e84ee4-92c2-483f-bc85-cc2ee3ee0db3` |
| Environment | One `production` environment for both testing and public play; staging can come later |
| Service | `public-mountain` |
| Source | This repository, root directory `/`; manual deploys initially |
| Branch | Manual uploads from `multiplayer`; choose the source branch before enabling autodeploy |
| Builder | Root multi-stage `Dockerfile`, using `nix develop .#build` |
| Runtime | Node 24; run `node server/server.mjs` directly as PID 1 |
| Networking | Railway-generated HTTPS domain, with same-origin `/ws` upgrades |
| Replicas | **1, in one region**; no horizontal or multi-region scaling |
| Region | US East (Virginia), `us-east4-eqdc4a`, declared in `.railway/railway.ts` |
| Sleeping/serverless | Disabled: keep the room process running |
| Initial resource target | 1 vCPU / 512 MiB, subject to plan availability and measurement |
| Healthcheck | `/health`, startup timeout 60 seconds |
| Restart policy | On failure, maximum 10 retries; alert if exhausted |
| Database / Redis / volumes | None |
| Player admission | Existing 128-player cap; not a production throughput guarantee |

One service serves both the site and WebSockets, so no CORS, separate API domain,
or frontend backend-URL variable is needed. Railway terminates TLS; Node listens
on `0.0.0.0` using Railway's `PORT`. The client automatically selects `wss:`
when the site is served over HTTPS. Start with the generated domain; a custom
domain can be added later.

Use one environment for this personal project. Testing and public play share the
same mountain, and test deployments reset connected players like any other release.
If staging is added later, it will be a separate mountain. Two replicas in the
same environment would also create separate mountains, not one larger shared
room. Sticky sessions would not fix this. Increase resources vertically only
after measuring the simulation, snapshot serialization, and outbound bandwidth.

## Build and image contract

The production container has three stages, with base images pinned by digest:

1. **Build:** use the locked Nix build shell to supply emscripten, Python/pefile, make,
   curl, Node and npm libraries. Run `make web server`. The build downloads the
   original `ski32.exe` from its author's site, checks the SHA-256 in `Makefile`,
   and extracts the assets. A third-party download outage should fail the build,
   leaving the existing deployment in place. Cache toolchains and dependencies
   where possible; the dedicated build shell excludes the development shell's
   large reverse-engineering tools.
2. **Dependencies:** install locked runtime npm libraries using
   `npm ci --omit=dev --ignore-scripts`. The Railway SDK is development-only.
3. **Runtime:** include only Node, locked production npm dependencies,
   `server/server.mjs`, `package.json`, `LICENSE`, and the generated files listed below.
   No C compiler, SDL development packages, emscripten, or original executable
   should ship in this image. Run as a non-root user.

Required generated files, preserving paths relative to the application root:

```text
build/web/index.html
build/web/index.js
build/web/index.wasm
build/web/multiplayer.js
build/server/core.cjs
build/server/core.wasm
```

The image installs its own runtime dependencies rather than copying development
`node_modules` symlinks pointing into the Nix store. `.dockerignore` allowlists
only the source/build inputs. Local builds, `node_modules`, `.git`, `.amp`,
credential files and original binaries are not sent to the Docker builder.

The extracted graphics remain Chris Pirih's copyrighted material, as described
in the root README. Confirm public distribution is acceptable before launch;
the project's MIT license does not cover those assets.

## Variables and credentials

The game requires **no application secrets** for this milestone.

| Variable | Value / source |
| --- | --- |
| `PORT` | Supplied by Railway; do not pin the service to local port 8000 |
| `NODE_ENV` | `production` |
| `RAILWAY_DEPLOYMENT_OVERLAP_SECONDS` | `0` |
| `RAILWAY_DEPLOYMENT_DRAINING_SECONDS` | `5` |

Project IDs, service IDs, region and domain are configuration, not passwords.
The project ID is recorded above. The production service ID is
`ecadbfbf-a548-482f-8525-b6aad55aa154`, and its public domain is
`public-mountain-production.up.railway.app`. Do not add fake API keys to
application configuration.

Deployment needs Railway authentication, not an application secret. Either use
interactive `railway login`, or create a project token scoped to the `production`
environment of an existing Railway project and supply it as `RAILWAY_TOKEN` to
the deployment tooling. Store that token in Amp Secrets for agent-driven deploys
or the CI provider's secret store for future CI, never in this repository, a
Docker build argument, a committed `.env` file, or chat. The running game does
not need the token. No token is needed to build or test the application locally.

## Redeploy and failure semantics

All player state is ephemeral. A crash, restart, rollback or deployment removes
everyone from that process. Browsers retry after two seconds, get a new skier ID,
and start at the top of the same seeded course. Scores and run counters are lost.
The SIGTERM handler stops accepting HTTP traffic, terminates WebSockets and
shuts down the world. Active HTTP responses get at most four seconds to finish
before being destroyed, within Railway's five-second draining window.

Set overlap to zero to avoid intentionally keeping the old room alive after the
replacement becomes active. **This is not a distributed-singleton guarantee:**
Railway starts the new process for healthchecking before retiring the old one,
and existing WebSockets can briefly remain on the old process. Treat releases
as a room reset, not seamless state transfer. The five-second draining window
caps graceful shutdown time. If strict no-overlap behavior becomes necessary,
use a maintenance deployment that stops the old deployment before starting the
new one, accepting downtime, or implement coordination before scaling.

A failed startup healthcheck should leave the old deployment serving. Railway's
healthcheck is a deployment gate, **not continuous monitoring**. Add an external
uptime check and verify that the `/health` tick counter keeps increasing.
Rollback restores an image, not the previous world's in-memory state.

## Local verification (no Railway account needed)

```sh
nix develop
npm run railway:check
make test web-test network-test
docker build -t skifree:railway .
# Run the existing network suite against the shipped server, WASM and libraries.
docker run --rm --read-only \
  --mount type=bind,source="$(pwd)/tests/server.test.mjs",target=/app/tests/server.test.mjs,readonly \
  skifree:railway node --test tests/server.test.mjs
docker run -d --name skifree-check -e PORT=8123 -p 127.0.0.1:8123:8123 skifree:railway
curl --fail http://127.0.0.1:8123/health
docker exec skifree-check id -u  # 1000, not root
docker stop --time 5 skifree-check
docker inspect --format '{{.State.ExitCode}}' skifree-check  # 0, not SIGKILL/137
docker rm skifree-check
```

Use a local Docker engine for the image commands; it is not supplied by the Nix
shell. `railway:check` uses the SDK's offline evaluator and prints the desired
graph. It is **not** an authenticated Railway plan or a guarantee that an account
supports particular resource limits. SDK 3.5.10 is pinned to retain the IaC runner
used by the flake's CLI 5.30.4; newer SDKs move that engine into newer CLI releases.
Upgrade the SDK and CLI together, not the SDK alone. Both the full development
shell and smaller build shell consume the same npm lockfile through Nix.

While the container runs, also verify JS/WASM delivery and two real WebSocket
joins on `/ws`; use independent browsers to check movement. Stop it with players
connected to check shutdown within the draining window.

## Deployment operator workflow

1. Complete the local checks above. The project `skifree-multiplayer` already
   has one `production` environment and one deployed `public-mountain` service.
   Configuration applies and uploads change shared/paid resources; obtain
   authorization for subsequent changes before running them.
2. With the environment-scoped `RAILWAY_TOKEN` available to the CLI, run
   `railway status` to verify the project and environment. No login or explicit
   link is needed for this token: the token identifies both targets. The service
   is already provisioned; do not create a second room service.

   Alternatively, for interactive account authentication without a project token:

   ```sh
   nix develop
   railway login
   railway link --project '96e84ee4-92c2-483f-bc85-cc2ee3ee0db3' \
     --environment production
   railway status
   ```

3. Run `railway config plan` and review the exact resource changes before
   authorizing `railway config apply`. The initial apply created only one
   service, `public-mountain`. This is a whole-project configuration: omitting
   existing resources can delete them. Use a dedicated project, not one hosting
   unrelated services. Railway does not automatically apply `.railway/railway.ts`
   during a deploy. Do not introduce deprecated `railway.json` / `railway.toml`.
   CLI 5.30.4 can repeatedly report restart/sleep defaults as unset after apply;
   inspect the live service instance before treating that diff as a failed apply.
4. Verify US East (Virginia), `us-east4-eqdc4a`, is the **only** region with one
   replica, as declared by the IaC. Set initial resource limits based on the available plan
   (proposal: 1 vCPU / 512 MiB), plus spend/failure alerts. The IaC deliberately
   omits unbenchmarked resource limits; verify
   these settings before every rollout. No GitHub source is declared, so there
   is no GitHub autodeploy. Keep releases manual until a source branch and
   release/reset policy are agreed.
5. Once the image and settings are verified, authorize and run:

   ```sh
   railway up --service public-mountain --environment production
   ```

6. Keep the existing public domain and verify the acceptance checks below.
   Use this same environment for testing and public play. There is no separate
   staging deployment to provision or maintain for now.

## Acceptance and operations

Before any upload, run `make test web-test network-test` in the Nix shell and
test the final image itself (not just the development checkout): non-default
`PORT`, non-root startup, static JS/WASM delivery, real WebSocket joins, and
SIGTERM/exit within the five-second draining window.

After an authorized deployment:

- `curl --fail https://public-mountain-production.up.railway.app/health` returns 200 with capacity 128;
  successive responses show an advancing tick counter.
- Two independent browsers join the same population, share terrain and see each
  other moving. Developer tools show a successful `wss://.../ws` connection.
- Closing one browser removes only its skier; reopening starts a new skier.
- Verify the full yeti/eating/respawn cycle and the original `?single=1` fallback.
- An authorized restart/redeploy reconnects both browsers to the replacement room
  without leaving ghost players or a prolonged split population.
- Measure a representative sustained load before inviting broad traffic. Monitor
  CPU, memory, simulation tick rate, connection count and outbound bandwidth;
  densely packed players generate more snapshot traffic than dispersed players.
- Configure a spend budget/alerts and service failure monitoring before launch.
  Existing per-connection input limits are not complete public abuse protection.

Run these checks on the single `production` deployment before sharing it broadly.
The initial authorized rollout provisioned the service and public domain in the
existing Railway project. No CI deployment token, GitHub autodeploy, external
monitor, spend alert, database or persistent volume has been configured.

## References

- [Railway infrastructure as code](https://docs.railway.com/infrastructure-as-code)
- [Public networking and WebSocket limits](https://docs.railway.com/networking/public-networking/specs-and-limits)
- [Startup healthchecks](https://docs.railway.com/deployments/healthchecks)
- [Deployment overlap and draining](https://docs.railway.com/deployments/deployment-teardown)
