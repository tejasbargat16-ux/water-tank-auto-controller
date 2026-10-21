/*
 * Auto Water Tank Level Controller — ESP32 Version with Web Dashboard
 * HC-SR04 Ultrasonic Sensor + Relay (Pump) + Wi-Fi Web Dashboard
 *
 * Pin Mapping (ESP32):
 * - TRIG_PIN   : GPIO 18
 * - ECHO_PIN   : GPIO 19  (IMPORTANT: Use voltage divider: 5V Echo -> 1k/2k -> 3.3V GPIO 19)
 * - RELAY_PIN  : GPIO 23  (Active LOW relay module)
 * - BUZZER_PIN : GPIO 25
 * - LED_PIN    : GPIO 26
 *
 * Features:
 * - Median filter across 5 samples to eliminate spurious echo spikes
 * - Hysteresis band between FULL and LOW thresholds (stops relay chatter)
 * - Dry-run / continuous run safety cutoff (auto shutoff after max runtime)
 * - Responsive Web Dashboard hosted directly on ESP32 (Wi-Fi Station + AP Fallback)
 * - REST API endpoints (/api/status, /api/pump, /api/reset) for home automation
 * - Non-blocking timing for responsive web serving and smooth buzzer beeps
 */

#include <WiFi.h>
#include <WebServer.h>

// ==========================================
// 1. PIN & HARDWARE CONFIGURATION
// ==========================================
#define TRIG_PIN   18
#define ECHO_PIN   19
#define RELAY_PIN  23
#define BUZZER_PIN 25
#define LED_PIN    26

// Tank calibration (distances in cm from sensor to water surface)
const float FULL_DISTANCE = 10.0;   // cm, sensor to water surface when tank is FULL
const float LOW_DISTANCE  = 30.0;   // cm, sensor to water surface when tank is LOW

// Relay logic (Most relay modules are active LOW)
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// Median filter sample count
const int SAMPLES = 5;

// Safety cutoff: Maximum continuous pump runtime before tripping fault
const unsigned long MAX_PUMP_RUNTIME_MS = 15UL * 60UL * 1000UL; // 15 minutes

// Buzzer alert beep timing
const unsigned long BEEP_INTERVAL_MS = 500;
const unsigned long FULL_ALARM_DURATION_MS = 5000;
const unsigned long SENSOR_INTERVAL_MS = 1000;

// ==========================================
// 2. WI-FI CONFIGURATION
// ==========================================
// Enter your local Wi-Fi credentials here:
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Fallback SoftAP settings (active if local Wi-Fi connection fails)
const char* AP_SSID = "WaterTank-ESP32";
const char* AP_PASS = "12345678";

WebServer server(80);

// ==========================================
// 3. SYSTEM STATE VARIABLES
// ==========================================
unsigned long pumpStartTime = 0;
bool pumpRunning = false;
bool safetyTripped = false;
bool manualOverride = false;
bool manualPumpState = false;

unsigned long lastSensorReadTime = 0;
unsigned long lastBeepToggle = 0;
bool buzzerState = false;
bool alarmActive = false;

bool tankWasFull = false;
unsigned long fullAlarmStartTime = 0;

float currentDistance = 0.0;
int currentPercentage = 0;
String currentStatusText = "INITIALIZING";

// ==========================================
// 4. HTML/CSS/JS WEB DASHBOARD (Stored in Flash)
// ==========================================
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Smart Water Tank Controller</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg: #0f172a;
      --card: #1e293b;
      --card-border: #334155;
      --text: #f8fafc;
      --text-muted: #94a3b8;
      --water-color: #38bdf8;
      --water-wave: #0284c7;
      --success: #10b981;
      --warning: #f59e0b;
      --danger: #ef4444;
      --primary: #3b82f6;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
      background: radial-gradient(circle at top, #1e293b 0%, #0f172a 100%);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 1.5rem 1rem;
    }
    .header {
      text-align: center;
      margin-bottom: 2rem;
    }
    .header h1 {
      font-size: 1.8rem;
      font-weight: 700;
      color: #e2e8f0;
      letter-spacing: -0.5px;
    }
    .header p {
      font-size: 0.9rem;
      color: var(--text-muted);
      margin-top: 0.25rem;
    }
    .grid {
      display: grid;
      grid-template-columns: 1fr;
      gap: 1.5rem;
      width: 100%;
      max-width: 440px;
    }
    .card {
      background: rgba(30, 41, 59, 0.7);
      backdrop-filter: blur(12px);
      border: 1px solid var(--card-border);
      border-radius: 1rem;
      padding: 1.5rem;
      box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.4);
    }
    .tank-wrapper {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 1.25rem;
    }
    .tank {
      width: 140px;
      height: 220px;
      border: 4px solid #64748b;
      border-radius: 20px 20px 14px 14px;
      position: relative;
      overflow: hidden;
      background: #090d16;
      box-shadow: inset 0 0 15px rgba(0,0,0,0.8), 0 0 20px rgba(56, 189, 248, 0.15);
    }
    .tank-level {
      position: absolute;
      bottom: 0;
      left: 0;
      width: 100%;
      height: 0%;
      background: linear-gradient(180deg, #38bdf8 0%, #0369a1 100%);
      transition: height 0.8s ease-in-out;
      box-shadow: 0 -2px 10px rgba(56, 189, 248, 0.5);
    }
    .tank-markers {
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      pointer-events: none;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      padding: 10px 6px;
      font-size: 0.65rem;
      color: #cbd5e1;
      font-weight: 600;
    }
    .tank-marker-line {
      border-bottom: 1px dashed rgba(255,255,255,0.25);
      width: 100%;
      display: flex;
      justify-content: space-between;
    }
    .pct-display {
      font-size: 2.75rem;
      font-weight: 700;
      color: var(--water-color);
      line-height: 1;
    }
    .dist-display {
      font-size: 0.95rem;
      color: var(--text-muted);
      margin-top: 0.25rem;
    }
    .badge {
      display: inline-block;
      padding: 0.35rem 0.8rem;
      border-radius: 9999px;
      font-size: 0.75rem;
      font-weight: 700;
      letter-spacing: 0.5px;
      text-transform: uppercase;
    }
    .badge-on { background: rgba(16, 185, 129, 0.2); color: var(--success); border: 1px solid var(--success); }
    .badge-off { background: rgba(148, 163, 184, 0.2); color: var(--text-muted); border: 1px solid #475569; }
    .badge-alert { background: rgba(239, 68, 68, 0.2); color: var(--danger); border: 1px solid var(--danger); }
    .status-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 0.75rem 0;
      border-bottom: 1px solid rgba(255,255,255,0.06);
    }
    .status-row:last-child { border-bottom: none; }
    .status-label { font-size: 0.85rem; color: var(--text-muted); }
    .status-val { font-size: 0.95rem; font-weight: 600; }
    .btn-group {
      display: flex;
      gap: 0.75rem;
      margin-top: 1rem;
    }
    button {
      flex: 1;
      padding: 0.75rem 1rem;
      border: none;
      border-radius: 0.5rem;
      font-weight: 600;
      font-size: 0.85rem;
      cursor: pointer;
      transition: all 0.2s ease;
      background: #334155;
      color: var(--text);
    }
    button:hover { background: #475569; }
    button.btn-primary { background: #2563eb; color: #fff; }
    button.btn-primary:hover { background: #1d4ed8; }
    button.btn-danger { background: #dc2626; color: #fff; }
    button.btn-danger:hover { background: #b91c1c; }
    .footer {
      margin-top: 2rem;
      font-size: 0.75rem;
      color: #64748b;
      text-align: center;
    }
  </style>
</head>
<body>

  <div class="header">
    <h1>Water Tank Monitor</h1>
    <p>ESP32 Automated Ultrasonic Controller</p>
  </div>

  <div class="grid">
    <div class="card tank-wrapper">
      <div class="tank">
        <div class="tank-markers">
          <div class="tank-marker-line"><span>FULL</span><span>10cm</span></div>
          <div class="tank-marker-line"><span>75%</span></div>
          <div class="tank-marker-line"><span>50%</span></div>
          <div class="tank-marker-line"><span>LOW</span><span>30cm</span></div>
        </div>
        <div class="tank-level" id="tankLevel"></div>
      </div>
      <div style="text-align:center;">
        <div class="pct-display" id="pctVal">--%</div>
        <div class="dist-display" id="distVal">Distance: -- cm</div>
      </div>
    </div>

    <div class="card">
      <div class="status-row">
        <span class="status-label">Pump Status</span>
        <span id="pumpBadge" class="badge badge-off">OFF</span>
      </div>
      <div class="status-row">
        <span class="status-label">System Mode</span>
        <span id="modeVal" class="status-val">AUTO</span>
      </div>
      <div class="status-row">
        <span class="status-label">System Status</span>
        <span id="statusVal" class="status-val">Loading...</span>
      </div>
      <div class="status-row">
        <span class="status-label">Pump Runtime</span>
        <span id="runtimeVal" class="status-val">0s</span>
      </div>

      <div class="btn-group">
        <button id="btnAuto" class="btn-primary" onclick="setPumpMode('auto')">Auto Mode</button>
        <button id="btnToggle" onclick="togglePump()">Pump ON</button>
      </div>
      <div class="btn-group" id="resetGroup" style="display:none;">
        <button class="btn-danger" style="width:100%;" onclick="resetSafety()">Reset Safety Cutoff</button>
      </div>
    </div>
  </div>

  <div class="footer">
    ESP32 Dual-Core • Auto Hysteresis & Dry-Run Protected
  </div>

  <script>
    let isManual = false;
    let pumpRunning = false;

    async function fetchStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();

        // Level & Distance
        const pct = Math.max(0, Math.min(100, data.percentage));
        document.getElementById('tankLevel').style.height = pct + '%';
        document.getElementById('pctVal').textContent = (data.distance < 0) ? 'ERR' : pct + '%';
        document.getElementById('distVal').textContent = (data.distance < 0) 
          ? 'Sensor disconnected' 
          : `Distance: ${data.distance.toFixed(1)} cm`;

        // Pump & Safety Badges
        const pumpBadge = document.getElementById('pumpBadge');
        pumpRunning = data.pump;
        isManual = data.manual;

        if (data.safetyTripped) {
          pumpBadge.textContent = 'SAFETY CUTOFF';
          pumpBadge.className = 'badge badge-alert';
          document.getElementById('resetGroup').style.display = 'block';
        } else if (data.distance < 0) {
          pumpBadge.textContent = 'SENSOR ERROR';
          pumpBadge.className = 'badge badge-alert';
          document.getElementById('resetGroup').style.display = 'none';
        } else if (data.pump) {
          pumpBadge.textContent = 'RUNNING';
          pumpBadge.className = 'badge badge-on';
          document.getElementById('resetGroup').style.display = 'none';
        } else {
          pumpBadge.textContent = 'OFF';
          pumpBadge.className = 'badge badge-off';
          document.getElementById('resetGroup').style.display = 'none';
        }

        // Mode & Status
        document.getElementById('modeVal').textContent = isManual ? 'MANUAL OVERRIDE' : 'AUTOMATIC';
        document.getElementById('statusVal').textContent = data.statusText;
        document.getElementById('runtimeVal').textContent = data.runtimeSec + 's';

        // Toggle button label
        const btnToggle = document.getElementById('btnToggle');
        btnToggle.textContent = pumpRunning ? 'Stop Pump' : 'Start Pump';
        if (pumpRunning) {
          btnToggle.className = 'btn-danger';
        } else {
          btnToggle.className = '';
        }
      } catch (err) {
        console.error('Fetch error:', err);
      }
    }

    async function setPumpMode(mode) {
      await fetch(`/api/pump?state=${mode}`);
      fetchStatus();
    }

    async function togglePump() {
      const nextState = pumpRunning ? 'off' : 'on';
      await fetch(`/api/pump?state=${nextState}`);
      fetchStatus();
    }

    async function resetSafety() {
      await fetch('/api/reset');
      fetchStatus();
    }

    setInterval(fetchStatus, 2000);
    window.onload = fetchStatus;
  </script>
</body>
</html>
)rawliteral";

// ==========================================
// 5. HELPER FUNCTIONS
// ==========================================
float getSingleReading() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  // pulseIn with 30000us (~5m range) timeout
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    return -1;
  }

  return (duration * 0.0343) / 2.0;
}

// Median filter across SAMPLES readings
float getDistance() {
  float readings[SAMPLES];
  int validCount = 0;

  for (int i = 0; i < SAMPLES; i++) {
    float r = getSingleReading();
    if (r > 0) {
      readings[validCount] = r;
      validCount++;
    }
    delay(10);
  }

  if (validCount == 0) {
    return -1; // Sensor failed this cycle
  }

  // Sort valid readings
  for (int i = 0; i < validCount - 1; i++) {
    for (int j = 0; j < validCount - i - 1; j++) {
      if (readings[j] > readings[j + 1]) {
        float tmp = readings[j];
        readings[j] = readings[j + 1];
        readings[j + 1] = tmp;
      }
    }
  }

  return readings[validCount / 2];
}

int calculatePercentage(float distance) {
  if (distance < 0) return 0;
  if (distance <= FULL_DISTANCE) return 100;
  if (distance >= LOW_DISTANCE) return 0;
  float pct = ((LOW_DISTANCE - distance) / (LOW_DISTANCE - FULL_DISTANCE)) * 100.0;
  return constrain((int)pct, 0, 100);
}

void startPump() {
  if (safetyTripped) return;
  digitalWrite(RELAY_PIN, RELAY_ON);
  if (!pumpRunning) {
    pumpRunning = true;
    pumpStartTime = millis();
    Serial.println(F("-> Pump STARTED"));
  }
}

void stopPump() {
  digitalWrite(RELAY_PIN, RELAY_OFF);
  if (pumpRunning) {
    pumpRunning = false;
    Serial.println(F("-> Pump STOPPED"));
  }
}

void soundAlarm() {
  alarmActive = true;
  digitalWrite(LED_PIN, HIGH);
  if (millis() - lastBeepToggle >= BEEP_INTERVAL_MS) {
    lastBeepToggle = millis();
    buzzerState = !buzzerState;
    digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
  }
}

void silenceAlarm() {
  alarmActive = false;
  buzzerState = false;
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
}

// ==========================================
// 6. HTTP SERVER HANDLERS
// ==========================================
void handleRoot() {
  server.send(200, "text/html", DASHBOARD_HTML);
}

void handleStatusApi() {
  unsigned long runtimeSec = pumpRunning ? ((millis() - pumpStartTime) / 1000) : 0;

  String json = "{";
  json += "\"distance\":" + String(currentDistance, 1) + ",";
  json += "\"percentage\":" + String(currentPercentage) + ",";
  json += "\"pump\":" + String(pumpRunning ? "true" : "false") + ",";
  json += "\"safetyTripped\":" + String(safetyTripped ? "true" : "false") + ",";
  json += "\"manual\":" + String(manualOverride ? "true" : "false") + ",";
  json += "\"runtimeSec\":" + String(runtimeSec) + ",";
  json += "\"statusText\":\"" + currentStatusText + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handlePumpApi() {
  if (server.hasArg("state")) {
    String state = server.arg("state");
    if (state == "on") {
      manualOverride = true;
      startPump();
    } else if (state == "off") {
      manualOverride = true;
      stopPump();
    } else if (state == "auto") {
      manualOverride = false;
    }
  }
  handleStatusApi();
}

void handleResetApi() {
  safetyTripped = false;
  manualOverride = false;
  silenceAlarm();
  handleStatusApi();
}

// ==========================================
// 7. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  Serial.println(F("\n=========================================="));
  Serial.println(F(" Smart Water Tank Controller (ESP32)"));
  Serial.println(F("=========================================="));

  // Connect to Wi-Fi
  Serial.print(F("Connecting to Wi-Fi: "));
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\nWi-Fi Connected!"));
    Serial.print(F("Dashboard URL: http://"));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("\nLocal Wi-Fi unavailable. Starting Fallback SoftAP..."));
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print(F("AP SSID: "));
    Serial.println(AP_SSID);
    Serial.print(F("AP Dashboard URL: http://"));
    Serial.println(WiFi.softAPIP());
  }

  // Web routes
  server.on("/", handleRoot);
  server.on("/api/status", handleStatusApi);
  server.on("/api/pump", handlePumpApi);
  server.on("/api/reset", handleResetApi);
  server.begin();
  Serial.println(F("HTTP Web Server ready."));
}

// ==========================================
// 8. MAIN LOOP
// ==========================================
void loop() {
  // Handle Web Client requests
  server.handleClient();

  // Buzzer non-blocking toggle
  if (alarmActive) {
    soundAlarm();
  }

  // Periodic sensor reading and automation logic
  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorReadTime >= SENSOR_INTERVAL_MS) {
    lastSensorReadTime = currentMillis;

    currentDistance = getDistance();
    currentPercentage = calculatePercentage(currentDistance);

    // Fail-safe: sensor failure
    if (currentDistance < 0) {
      stopPump();
      silenceAlarm();
      currentStatusText = "SENSOR ERROR";
      Serial.println(F("STATUS: Sensor Error - Pump OFF"));
      return;
    }

    // Safety dry-run cutoff check
    if (pumpRunning && (currentMillis - pumpStartTime > MAX_PUMP_RUNTIME_MS)) {
      safetyTripped = true;
    }

    if (safetyTripped) {
      stopPump();
      soundAlarm();
      currentStatusText = "SAFETY CUTOFF TRIPPED";
      Serial.println(F("STATUS: Safety Cutoff - Pump ran past limit!"));
      return;
    }

    // Manual Override check
    if (manualOverride) {
      currentStatusText = pumpRunning ? "MANUAL (PUMP ON)" : "MANUAL (PUMP OFF)";
      return;
    }

    // Tank Full
    if (currentDistance <= FULL_DISTANCE) {
      stopPump();
      currentStatusText = "TANK FULL";

      if (!tankWasFull) {
        tankWasFull = true;
        fullAlarmStartTime = currentMillis;
        soundAlarm();
      } else {
        if (currentMillis - fullAlarmStartTime < FULL_ALARM_DURATION_MS) {
          soundAlarm();
        } else {
          silenceAlarm();
          digitalWrite(LED_PIN, HIGH); // Solid LED for full tank
        }
      }
      Serial.println(F("STATUS: TANK FULL - Pump OFF"));
    }
    // Water Low
    else if (currentDistance >= LOW_DISTANCE) {
      tankWasFull = false;
      silenceAlarm();
      startPump();
      currentStatusText = "WATER LOW - FILLING";
      Serial.println(F("STATUS: WATER LOW - Pump ON"));
    }
    // Hysteresis Band
    else {
      tankWasFull = false;
      silenceAlarm();
      if (pumpRunning) {
        currentStatusText = "FILLING TANK";
        Serial.println(F("STATUS: FILLING TANK (Pump ON)"));
      } else {
        currentStatusText = "NORMAL (IDLE)";
        Serial.println(F("STATUS: NORMAL LEVEL (Pump IDLE)"));
      }
    }
  }
}
