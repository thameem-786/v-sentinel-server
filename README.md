# V - Sentinel | ESP32 Connection Guide

## Files in this package

```
esp32_glove.ino   → Flash to your ESP32
server.js         → Run on your PC / laptop
package.json      → Node.js dependencies
```

---

## STEP 1 — Install Arduino Libraries

Open Arduino IDE → Tools → Manage Libraries → search and install:

| Library | Author | Version |
|---|---|---|
| WebSockets | Markus Sattler | 2.4.0+ |
| ArduinoJson | Benoit Blanchon | 6.x |

Board: **ESP32 Dev Module**
(Install ESP32 boards: File → Preferences → add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`)

---

## STEP 2 — Edit esp32_glove.ino

Change these 3 lines at the top:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* SERVER_HOST   = "192.168.1.100";   // ← your PC IP
```

**How to find your PC IP:**
- Windows: open CMD → type `ipconfig` → look for IPv4 Address
- Mac/Linux: open Terminal → type `ifconfig` → look for inet

---

## STEP 3 — Run the Node.js Server

```bash
# Install Node.js from https://nodejs.org if not already installed

cd esp32_glove
npm install
node server.js
```

You will see:
```
✅ Server running on port 3000
   In V-Sentinel app, use URL:
   ► ws://192.168.1.100:3000
```

---

## STEP 4 — Flash ESP32

1. Connect ESP32 via USB
2. Select correct COM port in Arduino IDE
3. Upload `esp32_glove.ino`
4. Open Serial Monitor (115200 baud)
5. You should see:
```
✅ WiFi connected!
   IP Address : 192.168.1.105
✅ WebSocket connected to V-Sentinel server
```

---

## STEP 5 — Open V-Sentinel App

1. Open `v-sentinel.html` in browser
2. Login as Supervisor (SUP-001 / admin123)
3. In the WebSocket URL box type: `ws://192.168.1.100:3000`
4. Click **Connect**
5. Live ESP32 data appears in the feed!

---

## Wiring Reference

```
ESP32 Pin   →  Component
─────────────────────────
GPIO 34     →  Piezo/capacitive sensor (signal pin)
GPIO 35     →  Battery voltage divider (center tap)
GPIO 2      →  Built-in LED (status indicator)
GPIO 4      →  Buzzer + (optional)
GND         →  All GND rails
3.3V        →  Sensor VCC
```

### Battery Voltage Divider (for GPIO 35)
```
Battery+ ──┬── 100kΩ ──┬── 100kΩ ── GND
           │           │
           │         GPIO 35
           │         (max 3.3V!)
```
This scales 4.2V → ~2.1V which is safe for ESP32 ADC.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| ESP32 won't connect to WiFi | Check SSID/password, must be 2.4GHz |
| WebSocket keeps disconnecting | Check PC firewall, allow port 3000 |
| No data in browser | Make sure server.js is running first |
| "Invalid URL" in app | Use `ws://` not `http://` |
| Serial shows IP but no WS connect | Double-check SERVER_HOST IP in .ino |

---

## Data format sent by ESP32

```json
{
  "type": "new_message",
  "device_id": "SG-2024-001",
  "message": "PKT#42 | BAT:72% | TEMP:34.2C | FIELD:0.00kV | RSSI:-52dBm | ✅ SAFE",
  "battery": 72,
  "temperature": 34.2,
  "field_kv": 0.0,
  "packet": 42,
  "timestamp": "2026-04-09T10:30:00Z"
}
```

Alert packet:
```json
{
  "type": "new_message",
  "device_id": "SG-2024-001",
  "message": "⚡ ELECTRIC FIELD DETECTED | 8.40 kV | BAT:72%",
  "is_alert": true,
  "alert_type": "FIELD_ALERT"
}
```
