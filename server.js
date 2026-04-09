/*
 * ═══════════════════════════════════════════════════════════
 *   V - SENTINEL  |  WebSocket Relay Server + UI Server
 * ═══════════════════════════════════════════════════════════
 */

const WebSocket = require('ws');
const http      = require('http');
const os        = require('os');
const fs        = require('fs');
const path      = require('path');

// ──────────────────────────────────────────────────────────
// CONFIG
// ──────────────────────────────────────────────────────────
const SERVER_PORT = process.env.PORT || 3000;
const MAX_HISTORY = 100;

// ──────────────────────────────────────────────────────────
// STATE
// ──────────────────────────────────────────────────────────
let messageHistory = [];
let esp32Clients   = new Map();
let browserClients = new Set();
let totalReceived  = 0;

// ──────────────────────────────────────────────────────────
// GET LOCAL IP
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
// HTTP SERVER (SERVE UI)
// ──────────────────────────────────────────────────────────
const server = http.createServer((req, res) => {
  if (req.url === "/") {
    const filePath = path.join(__dirname, "my.html");

    fs.readFile(filePath, (err, data) => {
      if (err) {
        res.writeHead(500);
        return res.end("Error loading UI");
      }

      res.writeHead(200, { "Content-Type": "text/html" });
      res.end(data);
    });
  } else {
    res.writeHead(404);
    res.end("Not Found");
  }
});

// ──────────────────────────────────────────────────────────
// WEBSOCKET SERVER
// ──────────────────────────────────────────────────────────
const wss = new WebSocket.Server({ server });

// ══════════════════════════════════════════════════════════
// CONNECTION HANDLER
// ══════════════════════════════════════════════════════════
wss.on('connection', (ws, req) => {
  const ip = req.socket.remoteAddress;
  ws._isAlive = true;
  ws._deviceId = null;
  ws._clientType = 'unknown';

  console.log(`\n[+] New connection from ${ip}`);

  ws.on('pong', () => { ws._isAlive = true; });

  ws.on('message', (data) => {
    let parsed;

    try {
      parsed = JSON.parse(data.toString());
    } catch {
      console.warn(`[WARN] Non-JSON message`);
      return;
    }

    const type = parsed.type || '';

    // ESP32 handshake
    if (type === 'handshake') {
      ws._clientType = 'esp32';
      ws._deviceId = parsed.device_id || 'ESP32';
      esp32Clients.set(ws._deviceId, ws);

      console.log(`[ESP32] Connected: ${ws._deviceId}`);

      ws.send(JSON.stringify({
        type: 'ack',
        message: `Connected to server`,
        timestamp: new Date().toISOString()
      }));
      return;
    }

    // Browser connect
    if (type === 'browser_connect') {
      ws._clientType = 'browser';
      browserClients.add(ws);

      ws.send(JSON.stringify({
        type: 'history',
        messages: messageHistory
      }));

      return;
    }

    // ESP32 data
    if (type === 'new_message') {
      totalReceived++;

      const enriched = {
        ...parsed,
        server_ts: new Date().toISOString(),
        packet_no: totalReceived
      };

      messageHistory.push(enriched);
      if (messageHistory.length > MAX_HISTORY) messageHistory.shift();

      for (const client of browserClients) {
        if (client.readyState === WebSocket.OPEN) {
          client.send(JSON.stringify(enriched));
        }
      }

      console.log(`[DATA] ${parsed.device_id}: ${parsed.message}`);
    }
  });

  ws.on('close', () => {
    if (ws._deviceId) esp32Clients.delete(ws._deviceId);
    browserClients.delete(ws);
    console.log(`[-] Disconnected`);
  });
});

// ──────────────────────────────────────────────────────────
// KEEP ALIVE
// ──────────────────────────────────────────────────────────
setInterval(() => {
  wss.clients.forEach(ws => {
    if (!ws._isAlive) return ws.terminate();
    ws._isAlive = false;
    ws.ping();
  });
}, 20000);

// ──────────────────────────────────────────────────────────
// START SERVER
// ──────────────────────────────────────────────────────────
server.listen(SERVER_PORT, () => {
  console.log("\n🚀 V-Sentinel Server Running");
  console.log(`🌐 Port: ${SERVER_PORT}`);
  console.log(`📡 Ready for ESP32 & Browser`);
});