import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { get } from 'node:http';
import { spawn } from 'node:child_process';
import { mkdtemp, mkdir, copyFile, symlink, open, readdir, readlink, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createInterface } from 'node:readline';
import { setTimeout as delay } from 'node:timers/promises';
import { runInNewContext } from 'node:vm';
import { WebSocket } from 'ws';
import { createPublicServer } from '../server/server.mjs';

function connect(port) {
  const socket = new WebSocket(`ws://127.0.0.1:${port}/ws`);
  const messages = [];
  let wake;
  socket.on('message', data => { messages.push(JSON.parse(data.toString())); wake?.(); });
  socket.on('error', () => {});
  return {
    socket,
    send: message => socket.send(JSON.stringify(message)),
    async next(predicate) {
      const deadline = Date.now() + 5000;
      while (Date.now() < deadline) {
        const index = messages.findIndex(predicate);
        if (index >= 0) return messages.splice(0, index + 1).at(-1);
        await new Promise(resolve => {
          const timer = setTimeout(resolve, 40);
          wake = () => { clearTimeout(timer); resolve(); };
        });
      }
      throw new Error('Timed out waiting for server message');
    },
  };
}
const self = (snapshot, id) => snapshot.players.find(p => p.id === id);

test('public mountain: shared terrain, authoritative inputs, nearby state, reset and rejoin', { timeout: 20000 }, async t => {
  const app = await createPublicServer({ port: 0 });
  t.after(() => app.close());
  const a = connect(app.port), b = connect(app.port);
  const wa = await a.next(m => m.type === 'welcome');
  const wb = await b.next(m => m.type === 'welcome');
  assert.notEqual(wa.id, wb.id);
  assert.deepEqual(wa.terrain, wb.terrain);
  assert.deepEqual(wa.sprites, wb.sprites);
  assert.ok(wa.terrain.length > 500);
  const initial = await a.next(m => m.type === 'snapshot' && m.population === 2);
  assert.equal(self(initial, wa.id).y, 0);
  assert.equal(self(initial, wb.id).y, 0);
  // A client-supplied ID is never used to select the skier receiving input.
  b.send({ type: 'key', key: 40, id: wa.id });
  const moved = await a.next(m => m.type === 'snapshot' && self(m, wb.id)?.y >= 45);
  assert.equal(self(moved, wa.id).y, 0);
  assert.ok(self(moved, wb.id).dy > 0);
  b.send({ type: 'aim', dx: 400, dy: 800 });
  const steered = await b.next(m => m.type === 'snapshot' && self(m, wb.id).x > self(initial, wb.id).x + 10);
  assert.ok(self(steered, wb.id).y > 0);
  b.send({ type: 'click' });
  await b.next(m => m.type === 'snapshot' && self(m, wb.id).z > 0);
  b.send({ type: 'key', key: 40 });
  a.send({ type: 'view', width: 320, height: 300 });
  // Landing can leave a skier facing sideways; keep pointing downhill,
  // including recovery from terrain collisions, as a player would.
  const downhill = setInterval(() => b.send({ type: 'key', key: 40 }), 200);
  let distant;
  try {
    distant = await a.next(m => m.type === 'snapshot' && m.population === 2 && !self(m, wb.id));
  } finally {
    clearInterval(downhill);
  }
  assert.equal(distant.players.length, 1);
  assert.equal(self(distant, wa.id).y, 0);
  b.send({ type: 'key', key: 113 });
  const reset = await a.next(m => m.type === 'snapshot' && m.tick > distant.tick && self(m, wb.id)?.y === 0);
  assert.equal(self(reset, wa.id).y, 0);
  assert.equal(self(reset, wb.id).z, 0);
  assert.equal(self(reset, wb.id).runs, 0);
  b.socket.close();
  await a.next(m => m.type === 'snapshot' && m.population === 1);
  const c = connect(app.port);
  const wc = await c.next(m => m.type === 'welcome');
  assert.notEqual(wc.id, wb.id);
  assert.deepEqual(wc.terrain, wa.terrain);
  await a.next(m => m.type === 'snapshot' && m.population === 2 && self(m, wc.id)?.y === 0);
  const health = await (await fetch(`http://127.0.0.1:${app.port}/health`)).json();
  assert.equal(health.players, 2);
  assert.ok(health.tick >= reset.tick);
  for (const path of ['/', '/?single=1', '/index.js', '/index.wasm', '/multiplayer.js'])
    assert.equal((await fetch(`http://127.0.0.1:${app.port}${path}`, { method: 'HEAD' })).status, 200);
  assert.equal((await fetch(`http://127.0.0.1:${app.port}/package.json`)).status, 404);
  assert.equal((await fetch(`http://127.0.0.1:${app.port}//[invalid`)).status, 400);
  assert.equal((await fetch(`http://127.0.0.1:${app.port}/health`)).status, 200);
});

test('public mountain rejects invalid/oversized/flooded input', { timeout: 20000 }, async t => {
  const app = await createPublicServer({ port: 0 });
  t.after(() => app.close());
  for (const input of ['not JSON', JSON.stringify({ type: 'position', x: 0, y: 32000 }), 'x'.repeat(300)]) {
    const peer = connect(app.port);
    await peer.next(m => m.type === 'welcome');
    const closed = once(peer.socket, 'close');
    peer.socket.send(input);
    const [code] = await closed;
    assert.equal(code, input.length > 256 ? 1009 : 1006);
  }
  const flood = connect(app.port);
  await flood.next(m => m.type === 'welcome');
  const flooded = once(flood.socket, 'close');
  for (let i = 0; i < 100; i++) flood.send({ type: 'key', key: 40 });
  assert.equal((await flooded)[0], 1006); // terminate, not a cooperative closing handshake
});

test('public mountain budgets ping and pong frames while answering ordinary pings', { timeout: 5000 }, async t => {
  const app = await createPublicServer({ port: 0 });
  t.after(() => app.close());
  for (const frame of ['ping', 'pong']) {
    const peer = connect(app.port);
    await peer.next(m => m.type === 'welcome');
    // Ordinary pings still receive their matching pong.
    const pong = once(peer.socket, 'pong');
    peer.socket.ping('hello');
    assert.equal((await pong)[0].toString(), 'hello');
    const closed = once(peer.socket, 'close');
    for (let i = 0; i < 100; i++) peer.socket[frame]('flood');
    assert.equal((await closed)[0], 1006);
  }
});

test('public mountain rejects overflow before upgrade and reuses released slots', { timeout: 20000 }, async t => {
  const app = await createPublicServer({ port: 0 });
  t.after(() => app.close());
  const peers = Array.from({ length: 128 }, () => connect(app.port));
  const welcomes = await Promise.all(peers.map(p => p.next(m => m.type === 'welcome')));
  assert.equal(new Set(welcomes.map(w => w.id)).size, 128);
  await peers[0].next(m => m.type === 'snapshot' && m.population === 128);
  // A raw upgrade client never acknowledges WebSocket close frames. Overflow
  // must be rejected at HTTP, rather than retaining an upgraded closing socket.
  const rejected = await Promise.all(Array.from({ length: 16 }, () => new Promise((resolve, reject) => {
    const request = get(`http://127.0.0.1:${app.port}/ws`, { headers: {
      Connection: 'Upgrade', Upgrade: 'websocket',
      'Sec-WebSocket-Version': '13', 'Sec-WebSocket-Key': 'dGhlIHNhbXBsZSBub25jZQ==',
    } });
    request.on('error', reject);
    request.on('response', response => { response.resume(); response.on('end', () => resolve(response.statusCode)); });
    request.on('upgrade', (response, socket) => { socket.destroy(); resolve(response.statusCode); });
  })));
  assert.deepEqual(rejected, Array(16).fill(503));
  peers[127].socket.close();
  await peers[0].next(m => m.type === 'snapshot' && m.population === 127);
  const replacement = connect(app.port);
  const welcome = await replacement.next(m => m.type === 'welcome');
  assert.ok(!welcomes.some(w => w.id === welcome.id));
  assert.deepEqual(welcome.terrain, welcomes[0].terrain);
  await peers[0].next(m => m.type === 'snapshot' && m.population === 128);
});

// Isolate a large, sparse static file so transfer cancellation/slow readers are
// exercised without changing the game's real assets or adding a test-only API.
async function largeFileServer(t) {
  const root = await mkdtemp(join(tmpdir(), 'skifree-http-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  await mkdir(join(root, 'server'));
  await mkdir(join(root, 'build/web'), { recursive: true });
  await copyFile(new URL('../server/server.mjs', import.meta.url), join(root, 'server/server.mjs'));
  await symlink(fileURLToPath(new URL('../build/server', import.meta.url)), join(root, 'build/server'), 'dir');
  await symlink(fileURLToPath(new URL('../node_modules', import.meta.url)), join(root, 'node_modules'), 'dir');
  const file = join(root, 'build/web/index.wasm');
  const handle = await open(file, 'w');
  await handle.truncate(64 * 1024 * 1024);
  await handle.close();
  const child = spawn(process.execPath, [join(root, 'server/server.mjs')], {
    env: { ...process.env, PORT: '0' }, stdio: ['ignore', 'pipe', 'pipe'],
  });
  let errors = '';
  child.stderr.on('data', data => { errors += data; });
  const exited = once(child, 'exit');
  t.after(async () => { if (child.exitCode === null) child.kill('SIGKILL'); await exited; });
  const lines = createInterface({ input: child.stdout });
  const [line] = await once(lines, 'line');
  assert.match(line, /Public mountain listening on \d+/, errors);
  return { child, exited, file, port: Number(line.split(' ').at(-1)) };
}

test('aborted static downloads release their file descriptors', {
  timeout: 10000, skip: process.platform !== 'linux' && '/proc file-descriptor inspection requires Linux',
}, async t => {
  const { child, file, port } = await largeFileServer(t);
  for (let i = 0; i < 12; i++) {
    await new Promise((resolve, reject) => {
      const request = get(`http://127.0.0.1:${port}/index.wasm`, response => {
        response.once('data', () => { response.destroy(); resolve(); });
      });
      request.on('error', reject);
    });
  }
  async function openDownloads() {
    const directory = `/proc/${child.pid}/fd`;
    const targets = await Promise.all((await readdir(directory)).map(fd => readlink(`${directory}/${fd}`).catch(() => '')));
    return targets.filter(target => target === file).length;
  }
  const deadline = Date.now() + 1000;
  while (await openDownloads() && Date.now() < deadline) await delay(20);
  assert.equal(await openDownloads(), 0, 'aborted responses must not retain the WASM file');
  assert.equal((await fetch(`http://127.0.0.1:${port}/health`)).status, 200);
});

test('SIGTERM exits cleanly within the drain window with a stalled HTTP reader', { timeout: 10000 }, async t => {
  const { child, exited, port } = await largeFileServer(t);
  const response = await new Promise((resolve, reject) => {
    get(`http://127.0.0.1:${port}/index.wasm`, resolve).on('error', reject);
  });
  response.pause();
  response.on('error', () => {});
  t.after(() => response.destroy());
  const started = performance.now();
  child.kill('SIGTERM');
  const [code, signal] = await exited;
  assert.equal(code, 0);
  assert.equal(signal, null);
  assert.ok(performance.now() - started < 4800, 'must exit before Railway force-kills the process at five seconds');
});

// Run the actual browser transport with a clock and a socket whose close never
// completes: recovery must not depend on a cooperative network or TCP timeout.
async function browserTransport(t) {
  t.mock.timers.enable({ apis: ['setTimeout', 'setInterval'] });
  let now = 0;
  class Socket {
    static OPEN = 1;
    static peers = [];
    readyState = 1;
    bufferedAmount = 0;
    sent = [];
    constructor() { Socket.peers.push(this); }
    send(data) { this.sent.push(JSON.parse(data)); }
    close() { this.readyState = 2; }
    receive(message) { this.onmessage({ data: JSON.stringify(message) }); }
  }
  const status = { dataset: {} }, controls = {}, events = {}, Module = {};
  runInNewContext(await readFile(new URL('../web/multiplayer.js', import.meta.url), 'utf8'), {
    Module, WebSocket: Socket, URLSearchParams, innerWidth: 1280, innerHeight: 720,
    location: { search: '', protocol: 'http:', host: 'localhost' },
    document: { getElementById: id => id === 'network-status' ? status : controls },
    performance: { now: () => now }, setTimeout, clearTimeout, setInterval, clearInterval,
    addEventListener: (name, handler) => { events[name] = handler; },
  });
  return {
    net: Module.skiMultiplayer, status, events, peers: Socket.peers,
    advance(ms) { now += ms; t.mock.timers.tick(ms); },
    join(socket, id) {
      socket.receive({ type: 'welcome', id, terrain: [], sprites: [], tickMs: 40 });
      socket.receive({ type: 'snapshot', population: 1, players: [
        { id, x: 0, y: 0, z: 0, dy: 0, style: 0, runs: 0, phase: 0, sprite: 0, yeti: { sprite: 0 } },
      ] });
    },
  };
}

test('browser transport recovers from silent connections without replaying stale controls', async t => {
  for (const stage of ['connecting', 'welcome', 'snapshot']) await t.test(stage, async t => {
    const { net, status, peers, advance, join } = await browserTransport(t);
    net.start();
    const old = peers[0];
    if (stage === 'welcome') old.receive({ type: 'welcome', id: 1, terrain: [], sprites: [], tickMs: 40 });
    if (stage === 'snapshot') join(old, 1);
    net.aim(400, 600);
    advance(4999);
    assert.notEqual(status.dataset.state, 'disconnected');
    advance(1);
    assert.equal(status.dataset.state, 'disconnected');
    assert.equal(net.id, 0);
    assert.equal(net.latest, null);
    // Late events must not resurrect the abandoned connection or schedule retries.
    join(old, 99);
    old.onclose({ code: 1006 });
    assert.equal(net.id, 0);
    advance(2000);
    assert.equal(peers.length, 2);
    join(peers[1], 2);
    assert.equal(status.dataset.state, 'connected');
    net.draw(1280, 720);
    assert.deepEqual(peers[1].sent, [{ type: 'view', width: 1280, height: 720 }]);
    net.key(40);
    assert.deepEqual(peers[1].sent.at(-1), { type: 'key', key: 40 });
  });
});

test('browser transport bounds queued controls and stops retrying on pagehide', async t => {
  const { net, status, peers, advance, join, events } = await browserTransport(t);
  net.start();
  join(peers[0], 1);
  const key = { type: 'key', key: 40 };
  peers[0].bufferedAmount = 16 * 1024 - JSON.stringify(key).length;
  net.key(40);
  assert.deepEqual(peers[0].sent, [key]); // exactly at the limit is allowed
  peers[0].bufferedAmount++;
  net.key(40);
  assert.equal(peers[0].sent.length, 1);
  assert.equal(status.dataset.state, 'disconnected');
  events.pagehide();
  advance(10000);
  assert.equal(peers.length, 1);
  events.pageshow({ persisted: true });
  assert.equal(peers.length, 2);
  join(peers[1], 2);
  assert.equal(status.dataset.state, 'connected');
});
