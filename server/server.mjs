import { createServer } from 'node:http';
import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import { pipeline } from 'node:stream';
import { fileURLToPath } from 'node:url';
import { performance } from 'node:perf_hooks';
import { WebSocket, WebSocketServer } from 'ws';
import createCore from '../build/server/core.cjs';

const files = new Map([
  ['/', ['index.html', 'text/html']],
  ['/index.html', ['index.html', 'text/html']],
  ['/index.js', ['index.js', 'text/javascript']],
  ['/index.wasm', ['index.wasm', 'application/wasm']],
  ['/multiplayer.js', ['multiplayer.js', 'text/javascript']],
]);
const keys = new Set([33, 34, 35, 36, 37, 38, 39, 40, 45, 96, 97, 98, 99, 100, 102, 103, 104, 105, 113]);

export async function createPublicServer({ port = 8000 } = {}) {
  const core = await createCore();
  if (!core._server_init(1)) throw new Error('Cannot create mountain');
  const terrain = JSON.parse(core.UTF8ToString(core._server_terrain()));
  const clients = new Map();
  let tick = 0;

  const server = createServer(async (req, res) => {
    let path;
    try { path = new URL(req.url, 'http://localhost').pathname; }
    catch { res.writeHead(400).end(); return; }
    if (req.method !== 'GET' && req.method !== 'HEAD') {
      res.writeHead(405).end();
      return;
    }
    if (path === '/health') {
      res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
      res.end(JSON.stringify({ players: clients.size, tick, capacity: 128 }));
      return;
    }
    const file = files.get(path);
    if (!file) { res.writeHead(404).end(); return; }
    const url = new URL(`../build/web/${file[0]}`, import.meta.url);
    try {
      const info = await stat(url);
      if (res.destroyed) return;
      res.writeHead(200, { 'Content-Type': file[1], 'Content-Length': info.size, 'Cache-Control': 'no-cache' });
      if (req.method === 'HEAD') res.end();
      else pipeline(createReadStream(url), res, () => {}); // tears down both ends on disconnect/error
    } catch {
      if (!res.destroyed) res.writeHead(404).end();
    }
  });
  const sockets = new WebSocketServer({
    server, path: '/ws', maxPayload: 256, maxFragments: 16,
    perMessageDeflate: false, autoPong: false, closeTimeout: 1000,
    // Synchronous admission: include closing peers and never upgrade overflow sockets.
    verifyClient: (_info, done) => done(sockets.clients.size < 128, 503, 'Mountain full'),
  });
  sockets.on('connection', (ws) => {
    ws.on('error', () => {}); // socket failure is cleaned up by close
    const id = core._server_join();
    if (!id) { ws.terminate(); return; }
    const client = { id, width: 1280, height: 720, queue: [], aim: null, tokens: 64, alive: true };
    clients.set(ws, client);
    ws.on('close', () => { clients.delete(ws); core._server_leave(id); });
    function acceptInput() {
      if (ws.readyState !== WebSocket.OPEN) return false;
      if (--client.tokens >= 0) return true;
      ws.terminate();
      return false;
    }
    ws.on('ping', data => { if (acceptInput()) ws.pong(data); });
    ws.on('pong', () => { if (acceptInput()) client.alive = true; });
    ws.on('message', (data, binary) => {
      if (!acceptInput()) return;
      if (binary) { ws.terminate(); return; }
      let message;
      try { message = JSON.parse(data.toString()); }
      catch { ws.terminate(); return; }
      if (message?.type === 'key' && Number.isInteger(message.key) && keys.has(message.key)) {
        if (client.queue.length < 8) client.queue.push(message);
      } else if (message?.type === 'aim' && Number.isInteger(message.dx) && Number.isInteger(message.dy) &&
                 Math.abs(message.dx) <= 2048 && Math.abs(message.dy) <= 2048) {
        client.aim = message;
      } else if (message?.type === 'click') {
        if (client.queue.length < 8) client.queue.push(message);
      } else if (message?.type === 'view' && Number.isInteger(message.width) && Number.isInteger(message.height)) {
        client.width = Math.max(320, Math.min(4096, message.width));
        client.height = Math.max(300, Math.min(2160, message.height));
      } else {
        ws.terminate();
      }
    });
    ws.send(JSON.stringify({ type: 'welcome', id, tickMs: 40, ...terrain }));
  });

  function step() {
    for (const client of clients.values()) {
      client.tokens = Math.min(64, client.tokens + 4);
      if (client.aim) core._server_aim(client.id, client.aim.dx, client.aim.dy);
      client.aim = null;
      for (const input of client.queue) {
        if (input.type === 'click') core._server_click(client.id);
        else core._server_key(client.id, input.key);
      }
      client.queue.length = 0;
    }
    core._server_step();
    tick++;
    const players = JSON.parse(core.UTF8ToString(core._server_players()));
    const byId = new Map(players.map(p => [p.id, p]));
    for (const [ws, client] of clients) {
      if (ws.readyState !== WebSocket.OPEN) continue;
      if (ws.bufferedAmount > 256 * 1024) { ws.terminate(); continue; }
      const self = byId.get(client.id);
      const nearby = players.filter(p => p.id === client.id ||
        (Math.abs(p.x - self.x) < client.width / 2 + 256 &&
         Math.abs(p.y - self.y) < client.height + 256));
      ws.send(JSON.stringify({ type: 'snapshot', tick, population: players.length, players: nearby }));
    }
  }
  try {
    await new Promise((resolve, reject) => {
      server.once('error', reject);
      server.listen(port, '0.0.0.0', resolve);
    });
  } catch (error) {
    sockets.close();
    core._server_destroy();
    throw error;
  }
  // Fixed simulation steps with bounded catch-up, never one world's pause per tab.
  let next = performance.now() + 40;
  const timer = setInterval(() => {
    let steps = 0;
    const now = performance.now();
    while (now >= next && steps++ < 5) { step(); next += 40; }
    if (now >= next) next = now + 40;
  }, 10);
  const heartbeat = setInterval(() => {
    for (const [ws, client] of clients) {
      if (!client.alive) ws.terminate();
      else { client.alive = false; ws.ping(); }
    }
  }, 10000);

  return {
    port: server.address().port,
    async close() {
      clearInterval(timer);
      clearInterval(heartbeat);
      const httpClosed = new Promise(resolve => server.close(resolve));
      const deadline = setTimeout(() => server.closeAllConnections(), 4000);
      const closed = new Promise(resolve => sockets.close(resolve));
      for (const ws of sockets.clients) ws.terminate();
      await Promise.all([closed, httpClosed]);
      clearTimeout(deadline);
      core._server_destroy();
    },
  };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const app = await createPublicServer({ port: Number(process.env.PORT || 8000) });
  console.log(`Public mountain listening on ${app.port}`);
  for (const signal of ['SIGTERM', 'SIGINT'])
    process.once(signal, async () => { await app.close(); process.exit(0); });
}
