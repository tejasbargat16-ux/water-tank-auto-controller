# Auto Water Tank Level Controller

An automated, intelligent water tank filling system using an ultrasonic sensor and a relay-controlled pump. Features dual platform support for both **ESP32** (with embedded Wi-Fi Web Dashboard and REST API) and **Arduino UNO** (standalone with Serial commands).

---

## Key Features

- **Ultrasonic Distance Sensing**: Measures the air gap between the sensor and the water surface to calculate real-time water level and fill percentage.
- **Median Filtering (5 Samples)**: Filters out acoustic noise, false echoes, and surface ripples without external filtering libraries.
- **Hysteresis Band**: Prevents relay chatter and rapid cycling around cutoff thresholds.
- **Dry-Run & Timeout Safety Cutoff**: Automatically disables the pump and sounds an alarm if the pump runs continuously past a maximum runtime (e.g. 15 minutes) without the tank reaching FULL.
- **Smart Alarm**: Beeps buzzer and blinks LED upon fault/dry-run detection; plays a brief 5-second chime on tank full before muting to prevent household disruption.
- **Dual Firmware Options**:
  - **ESP32 (`water_tank_controller.ino`)**: Modern dark-mode web dashboard, SoftAP fallback, manual overrides, and REST API endpoints.
  - **Arduino UNO (`water_tank_controller_uno.ino`)**: Lightweight, robust, non-blocking offline controller with Serial Monitor commands.
- **Wokwi Simulator Ready**: Run and test the complete circuit directly in your browser or VS Code using the included `diagram.json`.

---

## Hardware Requirements

- **Microcontroller**: ESP32 Dev Board **OR** Arduino UNO
- **Ultrasonic Sensor**: HC-SR04 (or waterproof JSN-SR04T)
- **Relay Module**: 5V 1-channel relay (Active LOW)
- **Water Pump**: 5V / 12V / AC submersible or external pump connected via relay contacts
- **Alarm Components**: 5V Active Buzzer, LED + 220Ω resistor
- **Voltage Divider (For ESP32 only)**: 1kΩ and 2kΩ resistors (to safely step down HC-SR04 5V ECHO to 3.3V)
- **Power Supply**: 5V DC power supply, breadboard/perfboard, and jumper wires

---

## Pin Mapping

| Component Pin | ESP32 GPIO | Arduino UNO Pin | Notes |
|---|---|---|---|
| **HC-SR04 TRIG** | GPIO 18 | D9 | Output trigger pulse (10µs) |
| **HC-SR04 ECHO** | GPIO 19 | D10 | **ESP32 Note:** Use voltage divider (5V -> 3.3V) |
| **Relay IN** | GPIO 23 | D7 | Active LOW (PUMP ON = LOW) |
| **Buzzer (+)** | GPIO 25 | D6 | Active buzzer |
| **Status LED (+)** | GPIO 26 | D5 | Connect via 220Ω current-limiting resistor |
| **VCC & GND** | 5V / GND | 5V / GND | Common ground across all components |

> [!WARNING]
> **ESP32 3.3V Logic Protection**: HC-SR04 ECHO outputs a 5V logic pulse. Connect ECHO to a voltage divider before GPIO 19:
> `ECHO (5V) ---> [ 1 kΩ ] ---> GPIO 19 ---> [ 2 kΩ ] ---> GND`

---

## ESP32 Wi-Fi & Web Dashboard

When running on ESP32, the controller hosts a local web dashboard accessible from any phone, tablet, or PC on your network.

### Features
- **Visual Tank Gauge**: Animated water cylinder displaying current fill % and distance in cm.
- **Live Status & Badges**: Real-time pump state (`RUNNING`, `OFF`, `SAFETY CUTOFF`, `SENSOR ERROR`).
- **Control Buttons**: Toggle between Automatic mode and Manual Pump ON/OFF.
- **Safety Reset**: One-click reset for the safety dry-run cutoff.
- **SoftAP Fallback**: If local Wi-Fi is unavailable, the ESP32 automatically creates a hotspot:
  - **SSID**: `WaterTank-ESP32`
  - **Password**: `12345678`
  - **Default URL**: `http://192.168.4.1`

### REST API Endpoints
- `GET /api/status` — Returns live JSON status:
  ```json
  {
    "distance": 14.5,
    "percentage": 77,
    "pump": false,
    "safetyTripped": false,
    "manual": false,
    "runtimeSec": 0,
    "statusText": "NORMAL (IDLE)"
  }
  ```
- `GET /api/pump?state=on|off|auto` — Switch pump mode or trigger manual override.
- `GET /api/reset` — Reset safety dry-run cutoff and clear alarms.

---

## Arduino UNO Serial Commands

Connect the Arduino UNO via USB at **9600 baud** to monitor readings and send single-letter control commands via the Serial Monitor:

- `r` or `R`: **Reset** the safety cutoff after an error.
- `s` or `S`: **Status report** (pump state, runtime, override status).
- `p` or `P`: **Manual override toggle** (force pump ON or OFF).

---

## Calibration Guide

Every water tank has unique dimensions. Adjust these constants in the `.ino` files:

```cpp
const float FULL_DISTANCE = 10.0;   // Distance (cm) from sensor to water when tank is FULL
const float LOW_DISTANCE  = 30.0;   // Distance (cm) from sensor to water when tank is LOW
const unsigned long MAX_PUMP_RUNTIME_MS = 15UL * 60UL * 1000UL; // Safety cutoff time
```

1. **Full Distance (`FULL_DISTANCE`)**: Measure the air gap between the sensor face and the maximum desired water level.
2. **Low Distance (`LOW_DISTANCE`)**: Measure the air gap when the water level is low enough that the pump should start.
3. **Safety Timeout (`MAX_PUMP_RUNTIME_MS`)**: Time a standard fill cycle and set this value 20-30% higher so it only triggers on pipe leaks, empty supply, or pump failure.

---

## Online Simulation (Wokwi)

You can simulate the circuit without physical hardware:

1. Visit [Wokwi.com](https://wokwi.com) or install the Wokwi VS Code Extension.
2. Open this repository folder.
3. Start simulation via `diagram.json` and `wokwi.toml`.
4. Click on the HC-SR04 sensor in the simulation preview to drag the distance slider and watch the relay, LED, and buzzer respond in real time.

---

## Repository Files

- [water_tank_controller.ino](file:///c:/Users/tejas/tejas_bargat/water-tank-auto-controller/water_tank_controller.ino) — ESP32 firmware with embedded Wi-Fi Web Dashboard and REST API.
- [water_tank_controller_uno.ino](file:///c:/Users/tejas/tejas_bargat/water-tank-auto-controller/water_tank_controller_uno.ino) — Arduino UNO non-blocking firmware with Serial commands.
- [diagram.json](file:///c:/Users/tejas/tejas_bargat/water-tank-auto-controller/diagram.json) — Wokwi simulation circuit layout.
- [wokwi.toml](file:///c:/Users/tejas/tejas_bargat/water-tank-auto-controller/wokwi.toml) — Wokwi project configuration.
- [README.md](file:///c:/Users/tejas/tejas_bargat/water-tank-auto-controller/README.md) — Documentation and guide.

---

## License

MIT
