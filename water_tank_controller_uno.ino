/*
 * Auto Water Tank Level Controller — Arduino UNO version
 * HC-SR04 ultrasonic sensor + relay (pump)
 *
 * Basic idea: measure distance from sensor to water surface.
 * Small distance = tank full, large distance = tank low.
 * Turns pump ON/OFF with hysteresis so relay doesn't chatter
 * near the threshold.
 *
 * Note: UNO only has digital pins 0-13 (0/1 used by Serial),
 * so pins are remapped from the ESP32 version below.
 */

#define TRIG_PIN   9
#define ECHO_PIN   10
#define RELAY_PIN  7
#define BUZZER_PIN 6
#define LED_PIN    5

// Tank calibration - measure these for your own tank and adjust
const float FULL_DISTANCE = 10.0;   // cm, sensor to water when full
const float LOW_DISTANCE  = 30.0;   // cm, sensor to water when low

// Most cheap relay modules are active LOW
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// take a few ultrasonic readings and use the median instead of a single
// reading - HC-SR04 sometimes gives random spikes/echoes, this filters
// most of that out without needing a full averaging library
const int SAMPLES = 5;

// safety cutoff: if the pump has been running this long without the
// tank reaching FULL, something's wrong (dry tank, sensor stuck, pipe
// issue etc). Cut power and just alarm instead of running the pump dry.
const unsigned long MAX_PUMP_RUNTIME_MS = 15UL * 60UL * 1000UL; // 15 min

// buzzer beeps instead of a constant tone - much less annoying if
// nobody's around to switch it off for a while
const unsigned long BEEP_INTERVAL_MS = 500;

unsigned long pumpStartTime = 0;
bool pumpRunning = false;
bool safetyTripped = false;

unsigned long lastBeepToggle = 0;
bool buzzerState = false;

void setup() {
  Serial.begin(9600); // UNO's USB-serial chip is happier at 9600 than 115200

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
}

float getSingleReading() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    return -1;
  }

  return duration * 0.0343 / 2.0;
}

// median of SAMPLES readings, ignoring failed (-1) reads.
// bubble sort is fine here, SAMPLES is tiny
float getDistance() {
  float readings[SAMPLES];
  int validCount = 0;

  for (int i = 0; i < SAMPLES; i++) {
    float r = getSingleReading();
    if (r > 0) {
      readings[validCount] = r;
      validCount++;
    }
    delay(10); // small gap between pings so echoes don't overlap
  }

  if (validCount == 0) {
    return -1; // sensor totally failed this cycle
  }

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

void startPump() {
  digitalWrite(RELAY_PIN, RELAY_ON);
  if (!pumpRunning) {
    pumpRunning = true;
    pumpStartTime = millis();
  }
}

void stopPump() {
  digitalWrite(RELAY_PIN, RELAY_OFF);
  pumpRunning = false;
}

void soundAlarm() {
  digitalWrite(LED_PIN, HIGH);
  // beep instead of holding the buzzer HIGH forever
  if (millis() - lastBeepToggle >= BEEP_INTERVAL_MS) {
    lastBeepToggle = millis();
    buzzerState = !buzzerState;
    digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
  }
}

void silenceAlarm() {
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  buzzerState = false;
}

void loop() {

  float distance = getDistance();

  Serial.print("Water distance: ");
  Serial.print(distance);
  Serial.println(" cm");

  // sensor couldn't get a valid reading at all this cycle - fail safe,
  // pump off, don't guess
  if (distance < 0) {
    stopPump();
    silenceAlarm();
    Serial.println("Sensor error - pump OFF");
    delay(1000);
    return;
  }

  // pump has been running too long without hitting FULL - trip the
  // safety cutoff. stays tripped until distance drops below FULL again
  // (i.e. someone checks the tank/pipes and it actually starts filling)
  if (pumpRunning && (millis() - pumpStartTime > MAX_PUMP_RUNTIME_MS)) {
    safetyTripped = true;
  }

  if (safetyTripped) {
    stopPump();
    soundAlarm();
    Serial.println("SAFETY CUTOFF - pump ran too long, check tank/pipes");

    if (distance <= FULL_DISTANCE) {
      safetyTripped = false; // tank actually filled, reset and resume normal logic
    } else {
      delay(1000);
      return;
    }
  }

  // tank full
  if (distance <= FULL_DISTANCE) {
    stopPump();
    soundAlarm();
    Serial.println("TANK FULL - pump OFF");
  }

  // water low, start filling
  else if (distance >= LOW_DISTANCE) {
    startPump();
    silenceAlarm();
    Serial.println("WATER LOW - pump ON");
  }

  // between low and full - keep doing whatever it was already doing
  // (this is the hysteresis band, stops the relay from flapping on/off)
  else {
    if (pumpRunning) {
      startPump(); // keeps runtime timer going, doesn't re-trigger relay
    }
    silenceAlarm();
    Serial.println("FILLING TANK");
  }

  delay(1000);
}
