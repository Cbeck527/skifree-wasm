# skifree-wasm website

This directory contains the static Astro brochure site for
`https://cmb.software/skifree-wasm/`. It deploys as a Cloudflare Workers Static
Assets project with no Worker script and no browser-side JavaScript. The game
itself runs separately at `https://ski.becker.foo/`; this page only links to it.

## Develop

The repository's Nix development shell provides Node.js and pnpm. The
`packageManager` field in `package.json` selects the pinned pnpm release.

```console
nix develop
cd www
pnpm install --frozen-lockfile
pnpm dev
```

Astro serves the site at `http://localhost:4321/skifree-wasm/`.

The page uses Berkeley Mono when its licensed font files are present in
`src/assets/fonts/` (ignored by Git), and falls back to the system monospace
font otherwise.

## Check

```console
pnpm check
pnpm build
pnpm exec wrangler deploy --dry-run
```

`pnpm preview` builds the site and starts Wrangler's local server. Check
`/skifree-wasm/` for the site and a missing path such as
`/skifree-wasm/missing/` for a 404 response.

## Hosting contract

This project owns only the Cloudflare route `cmb.software/skifree-wasm/*`. The
separate index Worker must:

- own `cmb.software` as a Cloudflare Custom Domain; and
- redirect exact `/skifree-wasm` to `/skifree-wasm/` with a query-preserving
  `308`.

Deploy the index Worker first so the zone has proxied DNS for the route. This
project does not provide a `workers.dev` hostname and does not handle the bare
domain or exact `/skifree-wasm` path.

## Deploy

Authenticate Wrangler for the Cloudflare account that owns `cmb.software`, then
deploy manually from this directory:

```console
pnpm deploy
```

That command builds the nested `dist/skifree-wasm/` output before publishing it.
