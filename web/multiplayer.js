/* Transport and camera/render interpolation. The server owns all gameplay. */
(function () {
  const status = document.getElementById('network-status');
  const controls = document.getElementById('network-controls');
  const enabled = new URLSearchParams(location.search).get('single') !== '1';
  status.hidden = controls.hidden = !enabled;
  const keys = new Set([33, 34, 35, 36, 37, 38, 39, 40, 45, 96, 97, 98, 99, 100, 102, 103, 104, 105, 113]);
  const net = Module.skiMultiplayer = {
    enabled, id: 0, terrain: [], sprites: [], latest: null,
    socket: null, width: innerWidth, height: innerHeight,
  };
  let previous = new Map(), received = 0, tickMs = 40;
  let retry, watchdog, stopped = false, pendingAim = null, lastAim = 0, view = '';

  function connection(text, state) {
    status.textContent = text;
    status.dataset.state = state;
  }
  function disconnected(socket, full = false) {
    if (socket !== net.socket) return;
    clearInterval(watchdog);
    net.socket = null; // ignore delayed events from a dead transport
    net.id = 0;
    net.latest = null;
    previous.clear();
    pendingAim = null;
    socket.close();
    if (stopped) return;
    connection(full ? 'Mountain full. Retrying…' : 'Disconnected. Rejoining the mountain…', 'disconnected');
    retry = setTimeout(net.start, 2000);
  }
  function send(message) {
    const socket = net.socket;
    if (socket?.readyState !== WebSocket.OPEN || !net.id) return false;
    const data = JSON.stringify(message);
    if (performance.now() - received >= 5000 || socket.bufferedAmount + data.length > 16 * 1024) {
      disconnected(socket);
      return false;
    }
    socket.send(data);
    return true;
  }
  net.start = function () {
    clearTimeout(retry);
    clearInterval(watchdog);
    connection('Joining the public mountain…', 'joining');
    net.socket?.close();
    net.id = 0;
    net.latest = null;
    previous.clear();
    pendingAim = null;
    view = '';
    const socket = net.socket = new WebSocket(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws`);
    received = performance.now();
    watchdog = setInterval(() => {
      if (performance.now() - received >= 5000 || socket.bufferedAmount > 16 * 1024)
        disconnected(socket);
    }, 1000);
    socket.onmessage = function (event) {
      if (socket !== net.socket) return;
      const message = JSON.parse(event.data);
      if (message.type === 'welcome') {
        net.id = message.id;
        net.terrain = message.terrain;
        net.sprites = message.sprites;
        tickMs = message.tickMs;
        received = performance.now();
      } else if (message.type === 'snapshot') {
        previous = new Map((net.latest?.players || []).map(p => [p.id, p]));
        net.latest = message;
        received = performance.now();
        const self = message.players.find(p => p.id === net.id);
        const phase = self.phase === 2 ? 'Eaten! Back to the top shortly…' :
                      self.phase === 1 ? 'The yeti is coming!' : 'Public mountain';
        connection(`${phase} · ${message.population} online\n` +
          `You: Skier-${net.id} · ${Math.floor(self.y / 16)} m · ${Math.round(self.dy * 25 / 16)} m/s\n` +
          `Style: ${self.style} · Runs: ${self.runs}`, self.phase === 2 ? 'eaten' : self.phase === 1 ? 'chased' : 'connected');
      }
    };
    socket.onclose = function (event) {
      disconnected(socket, event.code === 1013);
    };
    socket.onerror = function () { /* close handles connection failures */ };
  };
  net.key = key => { if (keys.has(key)) send({ type: 'key', key }); };
  net.aim = function (x, y) {
    pendingAim = {
      type: 'aim',
      dx: Math.max(-2048, Math.min(2048, Math.round(x - net.width / 2))),
      dy: Math.max(-2048, Math.min(2048, Math.round(y - net.height / 3))),
    };
  };
  net.click = () => send({ type: 'click' });

  net.draw = function (width, height) {
    net.width = width;
    net.height = height;
    const currentView = `${width},${height}`;
    if (net.id && view !== currentView) {
      if (send({ type: 'view', width, height })) view = currentView;
    }
    const now = performance.now();
    if (pendingAim && now - lastAim >= tickMs) {
      send(pendingAim);
      pendingAim = null;
      lastAim = now;
    }
    if (!net.latest) return;
    const alpha = Math.min(1, (now - received) / tickMs);
    const players = net.latest.players.map(p => {
      const old = previous.get(p.id);
      // Snap respawns/restarts and phase transitions; never slide across the mountain.
      if (!old || old.runs !== p.runs || old.phase !== p.phase || Math.abs(old.y - p.y) > 256)
        return p;
      const lerp = (a, b) => a + (b - a) * alpha;
      return { ...p, x: lerp(old.x, p.x), y: lerp(old.y, p.y), z: lerp(old.z, p.z),
        yeti: { ...p.yeti, x: lerp(old.yeti.x, p.yeti.x), y: lerp(old.yeti.y, p.yeti.y) } };
    });
    const self = players.find(p => p.id === net.id);
    if (!self) return;
    const items = net.terrain.map(([sprite, x, y]) => ({ sprite, x, y, z: 0 }));
    for (const p of players) {
      if (p.sprite) items.push(p);
      if (p.yeti.sprite) items.push({ ...p.yeti, z: 0 });
    }
    const depth = p => p.y - (p.sprite === 27 ? net.sprites[p.sprite][1] : 0);
    items.sort((a, b) => depth(a) - depth(b));
    const project = p => {
      const [w, h] = net.sprites[p.sprite];
      return [Math.round(p.x - self.x + width / 2 - w / 2),
              Math.round(p.y - p.z - self.y + height / 3 - h), w, h];
    };
    for (const p of items) {
      const [x, y, w, h] = project(p);
      if (x + w >= 0 && x < width && y + h >= 0 && y < height)
        Module._ski_web_sprite(p.sprite, x, y);
    }
    for (const p of players) {
      if (!p.sprite) continue;
      const [x, y, w] = project(p);
      if (x > -100 && x < width + 100 && y > 12 && y < height)
        Module._ski_web_label(p.id, x + w / 2, y - 12 - (p.id % 3) * 12, p.id === net.id ? 1 : 0);
    }
  };
  addEventListener('pagehide', () => {
    stopped = true;
    clearTimeout(retry);
    clearInterval(watchdog);
    if (net.socket) disconnected(net.socket);
  });
  addEventListener('pageshow', event => {
    if (event.persisted && enabled) { stopped = false; net.start(); }
  });
})();
