/*
 * Dual Water Tank Level Monitoring System
 * B.Tech ECE Mini-Project, TGPCET (RTMNU), Session 2026-27
 *
 * Team: Yashvant Rathod, Vijay Banarase, Tejas Bargat, Pratik Mahajan, Sahebrao Salgar
 * Guide: Mayuri Harde
 *
 * Microcontroller: Arduino Uno
 * Sensors: 2 x HC-SR04 Ultrasonic Sensors
 * Indicators: 2 x Common-Cathode RGB LEDs + 1 x 5V Buzzer
 *
 * Pin Mapping:
 * - Tank 1 TRIG : D2
 * - Tank 1 ECHO : D3
 * - Tank 1 RGB  : D8 (Red), D9 (Green), D10 (Blue)
 * - Tank 2 TRIG : D4
 * - Tank 2 ECHO : D5
 * - Tank 2 RGB  : D11 (Red), D12 (Green), D13 (Blue)
 * - Buzzer      : A0
 */

// Pin Definitions
#define TANK1_TRIG_PIN  2
#define TANK1_ECHO_PIN  3
#define TANK1_RGB_R     8
#define TANK1_RGB_G     9
#define TANK1_RGB_B     10

#define TANK2_TRIG_PIN  4
#define TANK2_ECHO_PIN  5
#define TANK2_RGB_R     11
#define TANK2_RGB_G     12
#define TANK2_RGB_B     13

#define BUZZER_PIN      A0

// Calibration Constants (Adjust according to physical tank height)
const float TANK_HEIGHT_CM = 40.0;   // Height of each tank in cm
const float MIN_DISTANCE   = 5.0;    // Sensor blind zone / maximum water level distance

// Level Thresholds (Percentage of capacity)
const int THRESHOLD_LOW  = 25;       // Below 25% = Low / Critical (Red LED + Buzzer)
const int THRESHOLD_FULL = 75;       // Above 75% = High / Full (Green LED)

const int SAMPLES = 3;               // Median filter samples per reading
const unsigned long UPDATE_INTERVAL_MS = 1000;
unsigned long lastUpdateMillis = 0;

// Measure single distance using HC-SR04
float readSingleDistance(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout (~5m)
  if (duration == 0) return -1;
  return (duration * 0.0343) / 2.0;
}

// Median distance reading to reject sensor spikes
float getMedianDistance(uint8_t trigPin, uint8_t echoPin) {
  float readings[SAMPLES];
  int count = 0;

  for (int i = 0; i < SAMPLES; i++) {
    float d = readSingleDistance(trigPin, echoPin);
    if (d > 0) {
      readings[count++] = d;
    }
    delay(10);
  }

  if (count == 0) return -1;

  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
      if (readings[j] > readings[j + 1]) {
        float temp = readings[j];
        readings[j] = readings[j + 1];
        readings[j + 1] = temp;
      }
    }
  }

  return readings[count / 2];
}

// Calculate level in percent (0 - 100%)
int calculateLevelPercent(float distance) {
  if (distance < 0) return 0;
  if (distance <= MIN_DISTANCE) return 100;
  if (distance >= TANK_HEIGHT_CM) return 0;

  float effectiveHeight = TANK_HEIGHT_CM - MIN_DISTANCE;
  float waterDepth = TANK_HEIGHT_CM - distance;
  int pct = (int)((waterDepth / effectiveHeight) * 100.0);
  return constrain(pct, 0, 100);
}

// Common-cathode RGB LED control (HIGH = ON)
void setRGBColor(uint8_t pinR, uint8_t pinG, uint8_t pinB, bool r, bool g, bool b) {
  digitalWrite(pinR, r ? HIGH : LOW);
  digitalWrite(pinG, g ? HIGH : LOW);
  digitalWrite(pinB, b ? HIGH : LOW);
}

void setup() {
  Serial.begin(9600);

  // Ultrasonic sensor pins
  pinMode(TANK1_TRIG_PIN, OUTPUT);
  pinMode(TANK1_ECHO_PIN, INPUT);
  pinMode(TANK2_TRIG_PIN, OUTPUT);
  pinMode(TANK2_ECHO_PIN, INPUT);

  // RGB LED pins
  pinMode(TANK1_RGB_R, OUTPUT);
  pinMode(TANK1_RGB_G, OUTPUT);
  pinMode(TANK1_RGB_B, OUTPUT);
  pinMode(TANK2_RGB_R, OUTPUT);
  pinMode(TANK2_RGB_G, OUTPUT);
  pinMode(TANK2_RGB_B, OUTPUT);

  // Buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Turn off all LEDs initially
  setRGBColor(TANK1_RGB_R, TANK1_RGB_G, TANK1_RGB_B, false, false, false);
  setRGBColor(TANK2_RGB_R, TANK2_RGB_G, TANK2_RGB_B, false, false, false);

  Serial.println(F("=================================================="));
  Serial.println(F(" Dual Water Tank Level Monitoring System (TGPCET) "));
  Serial.println(F("=================================================="));
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastUpdateMillis >= UPDATE_INTERVAL_MS) {
    lastUpdateMillis = currentMillis;

    // Read Tank 1
    float dist1 = getMedianDistance(TANK1_TRIG_PIN, TANK1_ECHO_PIN);
    int level1 = calculateLevelPercent(dist1);

    // Read Tank 2
    float dist2 = getMedianDistance(TANK2_TRIG_PIN, TANK2_ECHO_PIN);
    int level2 = calculateLevelPercent(dist2);

    bool buzzerAlert = false;

    // ----------------- TANK 1 INDICATION -----------------
    if (dist1 < 0) {
      // Sensor Error: Magenta (Red + Blue)
      setRGBColor(TANK1_RGB_R, TANK1_RGB_G, TANK1_RGB_B, true, false, true);
    } else if (level1 < THRESHOLD_LOW) {
      // Critical / Low Level: Red LED + Buzzer
      setRGBColor(TANK1_RGB_R, TANK1_RGB_G, TANK1_RGB_B, true, false, false);
      buzzerAlert = true;
    } else if (level1 < THRESHOLD_FULL) {
      // Medium / Normal Level: Yellow LED (Red + Green)
      setRGBColor(TANK1_RGB_R, TANK1_RGB_G, TANK1_RGB_B, true, true, false);
    } else {
      // Full Level: Green LED
      setRGBColor(TANK1_RGB_R, TANK1_RGB_G, TANK1_RGB_B, false, true, false);
    }

    // ----------------- TANK 2 INDICATION -----------------
    if (dist2 < 0) {
      // Sensor Error: Magenta (Red + Blue)
      setRGBColor(TANK2_RGB_R, TANK2_RGB_G, TANK2_RGB_B, true, false, true);
    } else if (level2 < THRESHOLD_LOW) {
      // Critical / Low Level: Red LED + Buzzer
      setRGBColor(TANK2_RGB_R, TANK2_RGB_G, TANK2_RGB_B, true, false, false);
      buzzerAlert = true;
    } else if (level2 < THRESHOLD_FULL) {
      // Medium / Normal Level: Yellow LED (Red + Green)
      setRGBColor(TANK2_RGB_R, TANK2_RGB_G, TANK2_RGB_B, true, true, false);
    } else {
      // Full Level: Green LED
      setRGBColor(TANK2_RGB_R, TANK2_RGB_G, TANK2_RGB_B, false, true, false);
    }

    // Buzzer Control
    digitalWrite(BUZZER_PIN, buzzerAlert ? HIGH : LOW);

    // Serial Logging
    Serial.print(F("[Tank 1] Dist: "));
    if (dist1 >= 0) { Serial.print(dist1, 1); Serial.print(F(" cm | Level: ")); Serial.print(level1); Serial.print(F("%")); }
    else { Serial.print(F("ERROR")); }

    Serial.print(F("  ---  [Tank 2] Dist: "));
    if (dist2 >= 0) { Serial.print(dist2, 1); Serial.print(F(" cm | Level: ")); Serial.print(level2); Serial.print(F("%")); }
    else { Serial.print(F("ERROR")); }

    Serial.print(F(" | Buzzer: "));
    Serial.println(buzzerAlert ? F("ALERT") : F("OFF"));
  }
}
