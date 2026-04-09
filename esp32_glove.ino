/*
 * ═══════════════════════════════════════════════════════════
 *   V - SENTINEL  |  Smart Safety Glove — ESP32 Firmware
 *   Device : Smart Glove (SG-2024-001)
 *   Author : Thameem / MCET ECE
 *   Board  : ESP32 DevKit v1
 * ═══════════════════════════════════════════════════════════
 *
 * LIBRARIES REQUIRED (install via Arduino Library Manager):
 *   1. WebSockets  by Markus Sattler  (v2.4.0+)
 *   2. ArduinoJson by Benoit Blanchon (v6.x)
 *
 * WIRING:
 *   Piezoelectric sensor  → GPIO 34 (ADC input)
 *   Battery voltage divider → GPIO 35 (ADC input)
 *   Status LED            → GPIO 2  (built-in LED)
 *   Buzzer (optional)     → GPIO 4
 * ═══════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// ──────────────────────────────────────────────────────────
// CONFIGURATION  ← change these to match your setup
// ──────────────────────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_WIFI_NAME";       // Your WiFi SSID
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";   // Your WiFi password
const char* SERVER_HOST   = "192.168.1.100";         // Your PC's local IP
const int   SERVER_PORT   = 3000;                    // Server port
const char* DEVICE_ID     = "SG-2024-001";           // Must match GLOVES data in app
const char* DEVICE_NAME   = "Smart Glove 1";

// ──────────────────────────────────────────────────────────
// PIN DEFINITIONS
// ──────────────────────────────────────────────────────────
#define PIN_PIEZO       34    // Capacitive / piezo field sensor (ADC)
#define PIN_BATTERY     35    // Battery voltage divider (ADC)
#define PIN_LED         2     // Status LED (built-in)
#define PIN_BUZZER      4     // Buzzer for alerts (optional)

// ──────────────────────────────────────────────────────────
// THRESHOLDS
// ──────────────────────────────────────────────────────────
#define FIELD_THRESHOLD   2000    // ADC raw value — triggers electric field alert
#define BAT_LOW_PERCENT   20      // Battery % — triggers low battery warning
#define BAT_CRITICAL      10      // Battery % — triggers critical alert
#define SEND_INTERVAL_MS  2000    // How often to send data (ms)
#define ALERT_INTERVAL_MS 500     // How often to send alerts (ms)

// ──────────────────────────────────────────────────────────
// GLOBALS
// ──────────────────────────────────────────────────────────
WebSocketsClient webSocket;

bool     wsConnected    = false;
uint32_t lastSendTime   = 0;
uint32_t lastAlertTime  = 0;
uint32_t packetCount    = 0;
bool     fieldAlert     = false;

// ──────────────────────────────────────────────────────────
// FUNCTION PROTOTYPES
// ──────────────────────────────────────────────────────────
void     webSocketEvent(WStype_t type, uint8_t* payload, size_t length);
void     sendSensorData();
void     sendAlert(const char* alertType, const char* message);
int      readBatteryPercent();
float    readFieldVoltage();
float    readTemperature();
void     blinkLED(int times, int delayMs);
String   buildJSON(const char* type, const char* message, bool isAlert);

// ══════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\n╔════════════════════════════╗");
  Serial.println(  "║   V - Sentinel  Firmware   ║");
  Serial.println(  "║   Smart Glove ESP32        ║");
  Serial.println(  "╚════════════════════════════╝");

  // Pin setup
  pinMode(PIN_LED,    OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  analogReadResolution(12);   // 12-bit ADC → 0–4095

  // Connect WiFi
  connectWiFi();

  // Connect WebSocket server
  webSocket.begin(SERVER_HOST, SERVER_PORT, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(15000, 3000, 2);  // ping every 15s

  Serial.println("► Connecting to V-Sentinel server...");
}

// ══════════════════════════════════════════════════════════
// LOOP
// ══════════════════════════════════════════════════════════
void loop() {
  webSocket.loop();

  if (!wsConnected) return;

  uint32_t now = millis();

  // ── Read sensors ──────────────────────────────────────
  int   rawField  = analogRead(PIN_PIEZO);
  float fieldKV   = (rawField / 4095.0) * 15.0;   // scale to 0–15 kV range
  int   batPct    = readBatteryPercent();
  float tempC     = readTemperature();
  long  rssi      = WiFi.RSSI();

  fieldAlert = (rawField > FIELD_THRESHOLD);

  // ── Send periodic sensor data ─────────────────────────
  if (now - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = now;
    sendSensorData(fieldKV, batPct, tempC, rssi);
  }

  // ── Send alert if electric field detected ────────────
  if (fieldAlert && (now - lastAlertTime >= ALERT_INTERVAL_MS)) {
    lastAlertTime = now;
    char msg[120];
    snprintf(msg, sizeof(msg),
      "⚡ ELECTRIC FIELD DETECTED | %.2f kV | BAT:%d%% | Zone:Active",
      fieldKV, batPct);
    sendAlert("FIELD_ALERT", msg);
    digitalWrite(PIN_BUZZER, HIGH);
    delay(100);
    digitalWrite(PIN_BUZZER, LOW);
  }

  // ── Battery alerts ────────────────────────────────────
  static uint32_t lastBatAlert = 0;
  if (batPct <= BAT_CRITICAL && now - lastBatAlert > 30000) {
    lastBatAlert = now;
    char msg[80];
    snprintf(msg, sizeof(msg), "🚨 CRITICAL BATTERY: %d%% | Please charge immediately!", batPct);
    sendAlert("BAT_CRITICAL", msg);
  } else if (batPct <= BAT_LOW_PERCENT && now - lastBatAlert > 60000) {
    lastBatAlert = now;
    char msg[80];
    snprintf(msg, sizeof(msg), "⚠️ LOW BATTERY: %d%% | Charge soon.", batPct);
    sendAlert("BAT_LOW", msg);
  }

  // ── Status LED ────────────────────────────────────────
  digitalWrite(PIN_LED, (millis() / 1000) % 2 == 0 ? HIGH : LOW);  // blink 1Hz
}

// ══════════════════════════════════════════════════════════
// WEBSOCKET EVENT HANDLER
// ══════════════════════════════════════════════════════════
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true;
      Serial.println("✅ WebSocket connected to V-Sentinel server");
      blinkLED(3, 150);
      // Send handshake
      webSocket.sendTXT("{\"type\":\"handshake\",\"device_id\":\"" + String(DEVICE_ID) + "\",\"name\":\"" + String(DEVICE_NAME) + "\",\"firmware\":\"v2.3.1\"}");
      break;

    case WStype_DISCONNECTED:
      wsConnected = false;
      Serial.println("❌ WebSocket disconnected. Retrying...");
      break;

    case WStype_TEXT:
      Serial.print("📩 Server says: ");
      Serial.println((char*)payload);
      break;

    case WStype_PING:
      Serial.println("🏓 Ping received");
      break;

    case WStype_PONG:
      Serial.println("🏓 Pong sent");
      break;

    default:
      break;
  }
}

// ══════════════════════════════════════════════════════════
// SEND SENSOR DATA
// ══════════════════════════════════════════════════════════
void sendSensorData(float fieldKV, int batPct, float tempC, long rssi) {
  packetCount++;

  StaticJsonDocument<256> doc;
  doc["type"]      = "new_message";
  doc["device_id"] = DEVICE_ID;

  char msg[160];
  snprintf(msg, sizeof(msg),
    "PKT#%lu | BAT:%d%% | TEMP:%.1fC | FIELD:%.2fkV | RSSI:%lddBm | %s",
    packetCount, batPct, tempC, fieldKV, rssi,
    fieldKV > 0.1 ? "⚡FIELD ACTIVE" : "✅ SAFE");
  doc["message"] = msg;

  // ISO timestamp
  char ts[30];
  snprintf(ts, sizeof(ts), "2026-04-09T%02lu:%02lu:%02luZ",
    (millis()/3600000) % 24,
    (millis()/60000)   % 60,
    (millis()/1000)    % 60);
  doc["timestamp"] = ts;

  // Extra telemetry fields (optional, server can use these)
  doc["battery"]     = batPct;
  doc["temperature"] = tempC;
  doc["field_kv"]    = fieldKV;
  doc["packet"]      = (int)packetCount;

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);

  Serial.println("📤 Sent: " + output);
}

// ══════════════════════════════════════════════════════════
// SEND ALERT
// ══════════════════════════════════════════════════════════
void sendAlert(const char* alertType, const char* message) {
  StaticJsonDocument<256> doc;
  doc["type"]       = "new_message";
  doc["device_id"]  = DEVICE_ID;
  doc["message"]    = message;
  doc["alert_type"] = alertType;
  doc["is_alert"]   = true;

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
  Serial.println("🚨 Alert sent: " + String(alertType));
}

// ══════════════════════════════════════════════════════════
// SENSOR HELPERS
// ══════════════════════════════════════════════════════════

// Read battery % from voltage divider on PIN_BATTERY
// Assumes: 3.7V LiPo, divider brings max 4.2V → 3.3V (ratio ~0.786)
// Adjust BAT_MAX_ADC / BAT_MIN_ADC for your hardware
int readBatteryPercent() {
  const int BAT_MAX_ADC = 2520;  // ADC at 4.2V (full)
  const int BAT_MIN_ADC = 1860;  // ADC at 3.0V (empty)
  int raw = analogRead(PIN_BATTERY);
  int pct = map(raw, BAT_MIN_ADC, BAT_MAX_ADC, 0, 100);
  return constrain(pct, 0, 100);
}

// Simple temperature estimate from ESP32 internal sensor
// Replace with DS18B20 or DHT22 for real temperature
float readTemperature() {
#ifdef CONFIG_IDF_TARGET_ESP32
  return temperatureRead();  // ESP32 internal sensor (~±3°C accuracy)
#else
  return 27.5;               // fallback for boards without internal sensor
#endif
}

// Blink status LED
void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(PIN_LED, HIGH); delay(delayMs);
    digitalWrite(PIN_LED, LOW);  delay(delayMs);
  }
}

// ══════════════════════════════════════════════════════════
// WIFI CONNECT
// ══════════════════════════════════════════════════════════
void connectWiFi() {
  Serial.print("► Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++attempts > 40) {
      Serial.println("\n❌ WiFi failed! Restarting ESP32...");
      ESP.restart();
    }
  }
  Serial.println("\n✅ WiFi connected!");
  Serial.print("   IP Address : "); Serial.println(WiFi.localIP());
  Serial.print("   Signal     : "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  blinkLED(2, 200);
}
