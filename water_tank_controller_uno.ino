/*
 * Auto Water Tank Level Controller — Arduino UNO version
 * HC-SR04 ultrasonic sensor + relay (pump)
 *
 * Basic idea: measure distance from sensor to water surface.
 * Small distance = tank full, large distance = tank low.
 * Turns pump ON/OFF with hysteresis so relay doesn't chatter
 * near the threshold.
 *
 * Pin Mapping (Arduino UNO):
 * - TRIG_PIN   : D9
 * - ECHO_PIN   : D10
 * - RELAY_PIN  : D7 (Active LOW relay module)
 * - BUZZER_PIN : D6
 * - LED_PIN    : D5
 *
 * Features:
 * - Median filter across 5 samples to eliminate spurious echo spikes
 * - Hysteresis band to prevent rapid relay cycling
 * - Dry-run & continuous run safety cutoff (auto shutoff after max runtime)
 * - Non-blocking timing for smooth buzzer beeping & responsive loop
 * - Smart Tank Full alert (beeps for 5 seconds on full, avoiding endless noise)
 * - Serial Monitor commands:
 *     'r' -> Reset safety cutoff
 *     's' -> Print system status report
 *     'p' -> Toggle pump manually (override)
 */

#define TRIG_PIN   9
#define ECHO_PIN   10
#define RELAY_PIN  7
#define BUZZER_PIN 6
#define LED_PIN    5

// Tank calibration - measure these for your own tank and adjust (in cm)
const float FULL_DISTANCE = 10.0;   // cm, sensor to water when full
const float LOW_DISTANCE  = 30.0;   // cm, sensor to water when low

// Most cheap relay modules are active LOW
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// Median filter sample count: HC-SR04 sometimes gives random spikes/echoes,
// taking the median of several samples filters them out cleanly.
const int SAMPLES = 5;

// Safety cutoff: if the pump runs continuously past this limit without
// reaching FULL, something's wrong (dry source, pipe burst, leak, sensor error).
// Cuts pump power to protect against dry running and triggers alarm.
const unsigned long MAX_PUMP_RUNTIME_MS = 15UL * 60UL * 1000UL; // 15 minutes

// Buzzer alert beep timing
const unsigned long BEEP_INTERVAL_MS = 500;

// Tank full alert chime duration (stops continuous beeping once full is detected)
const unsigned long FULL_ALARM_DURATION_MS = 5000;

// Sensor reading loop interval
const unsigned long SENSOR_INTERVAL_MS = 1000;

unsigned long pumpStartTime = 0;
bool pumpRunning = false;
bool safetyTripped = false;
bool manualOverride = false;

unsigned long lastSensorReadTime = 0;
unsigned long lastBeepToggle = 0;
bool buzzerState = false;
bool alarmActive = false;

bool tankWasFull = false;
unsigned long fullAlarmStartTime = 0;

void setup() {
  Serial.begin(9600); // UNO hardware serial
  while (!Serial && millis() < 1000); // short wait for native USB if applicable

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  Serial.println(F("========================================"));
  Serial.println(F(" Auto Water Tank Controller (Arduino UNO)"));
  Serial.println(F(" Commands: 'r'=Reset Safety | 's'=Status | 'p'=Toggle Pump"));
  Serial.println(F("========================================"));
}

float getSingleReading() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout (~5m max range)

  if (duration == 0) {
    return -1;
  }

  return (duration * 0.0343) / 2.0;
}

// Median of SAMPLES readings, ignoring failed (-1) reads.
float getDistance() {
  float readings[SAMPLES];
  int validCount = 0;

  for (int i = 0; i < SAMPLES; i++) {
    float r = getSingleReading();
    if (r > 0) {
      readings[validCount] = r;
      validCount++;
    }
    delay(10); // Small gap between pings so echoes don't overlap
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

void handleSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'r' || cmd == 'R') {
      safetyTripped = false;
      manualOverride = false;
      silenceAlarm();
      Serial.println(F("[COMMAND] Safety tripped flag RESET. Normal logic resumed."));
    } else if (cmd == 's' || cmd == 'S') {
      Serial.println(F("--- System Status ---"));
      Serial.print(F("Pump State: "));
      Serial.println(pumpRunning ? F("RUNNING") : F("OFF"));
      Serial.print(F("Safety Tripped: "));
      Serial.println(safetyTripped ? F("YES") : F("NO"));
      Serial.print(F("Manual Override: "));
      Serial.println(manualOverride ? F("YES") : F("NO"));
      if (pumpRunning) {
        Serial.print(F("Runtime: "));
        Serial.print((millis() - pumpStartTime) / 1000);
        Serial.println(F("s"));
      }
      Serial.println(F("---------------------"));
    } else if (cmd == 'p' || cmd == 'P') {
      manualOverride = true;
      if (pumpRunning) {
        stopPump();
        Serial.println(F("[COMMAND] Manual override: Pump OFF"));
      } else {
        startPump();
        Serial.println(F("[COMMAND] Manual override: Pump ON"));
      }
    }
  }
}

void loop() {
  // Handle incoming Serial commands
  handleSerialCommands();

  // Keep buzzer alert toggling smoothly without blocking delays
  if (alarmActive) {
    soundAlarm();
  }

  // Periodic sensor sampling & control logic
  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorReadTime >= SENSOR_INTERVAL_MS) {
    lastSensorReadTime = currentMillis;

    float distance = getDistance();
    int waterPercent = calculatePercentage(distance);

    Serial.print(F("Water Distance: "));
    if (distance >= 0) {
      Serial.print(distance, 1);
      Serial.print(F(" cm ("));
      Serial.print(waterPercent);
      Serial.print(F("%) | "));
    } else {
      Serial.print(F("ERROR | "));
    }

    // Fail-safe: sensor read failure
    if (distance < 0) {
      stopPump();
      silenceAlarm();
      Serial.println(F("STATUS: Sensor error - pump OFF"));
      return;
    }

    // Check runtime safety cutoff
    if (pumpRunning && (currentMillis - pumpStartTime > MAX_PUMP_RUNTIME_MS)) {
      safetyTripped = true;
    }

    if (safetyTripped) {
      stopPump();
      soundAlarm();
      Serial.println(F("STATUS: SAFETY CUTOFF - pump ran too long! Send 'r' to reset."));
      return;
    }

    // If user engaged manual override, skip auto state machine
    if (manualOverride) {
      Serial.println(F("STATUS: MANUAL OVERRIDE ACTIVE"));
      return;
    }

    // Tank Full condition
    if (distance <= FULL_DISTANCE) {
      stopPump();

      // Trigger full alarm chime on rising edge of tank full
      if (!tankWasFull) {
        tankWasFull = true;
        fullAlarmStartTime = currentMillis;
        soundAlarm();
      } else {
        // Stop beeping after FULL_ALARM_DURATION_MS, keep LED indicator
        if (currentMillis - fullAlarmStartTime < FULL_ALARM_DURATION_MS) {
          soundAlarm();
        } else {
          silenceAlarm();
          digitalWrite(LED_PIN, HIGH); // Steady LED indicates full tank
        }
      }
      Serial.println(F("STATUS: TANK FULL - pump OFF"));
    }
    // Water Low condition
    else if (distance >= LOW_DISTANCE) {
      tankWasFull = false;
      silenceAlarm();
      startPump();
      Serial.println(F("STATUS: WATER LOW - pump ON"));
    }
    // Hysteresis Band (Between FULL and LOW)
    else {
      tankWasFull = false;
      silenceAlarm();
      if (pumpRunning) {
        Serial.println(F("STATUS: FILLING TANK (Pump ON)"));
      } else {
        Serial.println(F("STATUS: NORMAL LEVEL (Pump IDLE)"));
      }
    }
  }
}
