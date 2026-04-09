/*
 * ═══════════════════════════════════════════════════════════
 *   V - SENTINEL  |  WebSocket Relay Server
 *   Bridges ESP32 hardware ↔ V-Sentinel browser dashboard
 *
 *   Run:  node server.js
 *   Port: 3000 (change SERVER_PORT below if needed)
 * ═══════════════════════════════════════════════════════════
 */

const WebSocket = require('ws');
const http      = require('http');
const os        = require('os');

// ──────────────────────────────────────────────────────────
// CONFIG
// ──────────────────────────────────────────────────────────
const SERVER_PORT    = 3000;
const MAX_HISTORY    = 100;   // Max messages to store for late-joining browsers

// ──────────────────────────────────────────────────────────
// STATE
// ──────────────────────────────────────────────────────────
let messageHistory = [];
let esp32Clients   = new Map();   // device_id → ws
let browserClients = new Set();   // browser dashboard connections
let totalReceived  = 0;

// ──────────────────────────────────────────────────────────
// DISPLAY LOCAL IP (so you know what to put in the app)
// ──────────────────────────────────────────────────────────
function getLocalIP() {
  const ifaces = os.networkInterfaces();
  for (const name of Object.keys(ifaces)) {
    for (const iface of ifaces[name]) {
      if (iface.family === 'IPv4' && !iface.internal) return iface.address;
    }
  }
  return 'localhost';
}

// ──────────────────────────────────────────────────────────
// CREATE HTTP + WS SERVER
// ──────────────────────────────────────────────────────────
const server = http.createServer((req, res) => {
  res.writeHead(200, { 'Content-Type': 'text/plain' });
  res.end('V-Sentinel WebSocket Server is running.\n');
});

const wss = new WebSocket.Server({ server });

// ══════════════════════════════════════════════════════════
// CONNECTION HANDLER
// ══════════════════════════════════════════════════════════
wss.on('connection', (ws, req) => {
  const ip = req.socket.remoteAddress;
  ws._isAlive    = true;
  ws._deviceId   = null;
  ws._clientType = 'unknown';   // 'esp32' | 'browser'

  console.log(`\n[+] New connection from ${ip}`);

  // ── Pong handler (keep-alive) ──────────────────────────
  ws.on('pong', () => { ws._isAlive = true; });

  // ── Message handler ────────────────────────────────────
  ws.on('message', (data) => {
    let parsed;
    try {
      parsed = JSON.parse(data.toString());
    } catch {
      console.warn(`[WARN] Non-JSON message from ${ip}:`, data.toString().substring(0, 80));
      return;
    }

    const type = parsed.type || '';

    // ── Handshake from ESP32 ─────────────────────────────
    if (type === 'handshake') {
      ws._clientType = 'esp32';
      ws._deviceId   = parsed.device_id || 'ESP32-UNKNOWN';
      esp32Clients.set(ws._deviceId, ws);

      console.log(`[ESP32] ✅ Handshake: ${ws._deviceId} (${parsed.name}) FW:${parsed.firmware}`);
      serverLog(`ESP32 ${ws._deviceId} connected`);

      // Acknowledge to ESP32
      safeSend(ws, JSON.stringify({
        type: 'ack',
        message: `Server acknowledged ${ws._deviceId}`,
        timestamp: new Date().toISOString()
      }));
      return;
    }

    // ── Browser requests history ─────────────────────────
    if (type === 'browser_connect') {
      ws._clientType = 'browser';
      browserClients.add(ws);
      console.log(`[Browser] 🖥️  Dashboard connected. Sending ${messageHistory.length} history items.`);

      safeSend(ws, JSON.stringify({
        type: 'history',
        messages: messageHistory
      }));

      safeSend(ws, JSON.stringify({
        type: 'server_status',
        esp32_devices: [...esp32Clients.keys()],
        total_received: totalReceived
      }));
      return;
    }

    // ── Data packet from ESP32 → relay to all browsers ───
    if (type === 'new_message') {
      if (!ws._clientType || ws._clientType === 'unknown') {
        ws._clientType = 'esp32';
        ws._deviceId   = parsed.device_id || 'ESP32';
        esp32Clients.set(ws._deviceId, ws);
      }

      totalReceived++;
      const enriched = {
        ...parsed,
        server_ts: new Date().toISOString(),
        packet_no: totalReceived
      };

      // Store in history (rolling window)
      messageHistory.push(enriched);
      if (messageHistory.length > MAX_HISTORY) messageHistory.shift();

      // Broadcast to all browser dashboards
      let sent = 0;
      for (const browser of browserClients) {
        if (browser.readyState === WebSocket.OPEN) {
          safeSend(browser, JSON.stringify({ type: 'new_message', ...enriched }));
          sent++;
        }
      }

      console.log(`[Data] 📨 ${parsed.device_id || 'ESP32'} → "${(parsed.message || '').substring(0,60)}" | Relayed to ${sent} browser(s)`);

      // If alert, log prominently
      if (parsed.is_alert) {
        console.log(`[ALERT] 🚨 ${parsed.alert_type} from ${parsed.device_id}`);
      }
    }
  });

  // ── Disconnection ──────────────────────────────────────
  ws.on('close', () => {
    if (ws._deviceId) esp32Clients.delete(ws._deviceId);
    browserClients.delete(ws);
    console.log(`[-] Disconnected: ${ws._clientType || 'unknown'} (${ws._deviceId || ip})`);
  });

  ws.on('error', (err) => {
    console.error(`[ERROR] ${ip}: ${err.message}`);
  });
});

// ══════════════════════════════════════════════════════════
// KEEP-ALIVE PING (every 20s)
// ══════════════════════════════════════════════════════════
setInterval(() => {
  wss.clients.forEach(ws => {
    if (!ws._isAlive) { ws.terminate(); return; }
    ws._isAlive = false;
    ws.ping();
  });
}, 20000);

// ══════════════════════════════════════════════════════════
// STATS LOG (every 30s)
// ══════════════════════════════════════════════════════════
setInterval(() => {
  console.log(`\n[Stats] ESP32: ${esp32Clients.size} | Browsers: ${browserClients.size} | Total packets: ${totalReceived}`);
}, 30000);

// ══════════════════════════════════════════════════════════
// HELPERS
// ══════════════════════════════════════════════════════════
function safeSend(ws, data) {
  try {
    if (ws.readyState === WebSocket.OPEN) ws.send(data);
  } catch (e) {
    console.error('[safeSend error]', e.message);
  }
}

function serverLog(msg) {
  const log = JSON.stringify({
    type: 'new_message',
    device_id: 'SERVER',
    message: `[SERVER] ${msg}`,
    timestamp: new Date().toISOString()
  });
  for (const browser of browserClients) safeSend(browser, log);
}

// ══════════════════════════════════════════════════════════
// START SERVER
// ══════════════════════════════════════════════════════════
server.listen(SERVER_PORT, () => {
  const localIP = getLocalIP();
  console.log('\n╔═════════════════════════════════════════╗');
  console.log('║       V - Sentinel  Relay Server        ║');
  console.log('╚═════════════════════════════════════════╝');
  console.log(`\n✅ Server running on port ${SERVER_PORT}`);
  console.log(`\n   In V-Sentinel app, use URL:`);
  console.log(`   ► ws://${localIP}:${SERVER_PORT}`);
  console.log(`\n   ESP32 SERVER_HOST in code:`);
  console.log(`   ► "${localIP}"`);
  console.log('\n   Waiting for ESP32 and browser connections...\n');
});
