# Auto Water Tank Level Controller

Automatic water tank filling system using an ultrasonic sensor and a relay-controlled pump. Built for ESP32 (with a ported Arduino UNO version included).

## Features

- Ultrasonic (HC-SR04) water level sensing
- Median filtering over 5 samples to reject noisy/spurious sensor readings
- Hysteresis between FULL and LOW thresholds — prevents relay chatter near the switch point
- Dry-run safety cutoff — auto-stops the pump and alarms if it runs continuously past a set time without the tank reaching FULL (protects against dry tank, blocked pipe, or stuck sensor)
- Beeping buzzer + LED alarm on tank full / fault, instead of a constant tone
- Fail-safe behavior — pump stays OFF if the sensor can't get a valid reading

## Hardware

- ESP32 dev board (or Arduino UNO — see `water_tank_controller_uno.ino`)
- HC-SR04 ultrasonic sensor
- 1-channel relay module (active LOW)
- 5V water pump / pump relay load
- Buzzer
- LED
- Jumper wires, breadboard/perfboard, 5V supply

## Pin Mapping

| Function | ESP32 | Arduino UNO |
|---|---|---|
| TRIG | GPIO 18 | D9 |
| ECHO | GPIO 19 | D10 |
| RELAY | GPIO 23 | D7 |
| BUZZER | GPIO 25 | D6 |
| LED | GPIO 26 | D5 |

## Files

- `water_tank_controller.ino` — ESP32 version
- `water_tank_controller_uno.ino` — Arduino UNO version

## Setup

1. Wire the components per the pin mapping table above.
2. Open the relevant `.ino` file in Arduino IDE.
3. Select the correct board (ESP32 Dev Module / Arduino Uno) and COM port.
4. Adjust `FULL_DISTANCE`, `LOW_DISTANCE`, and `MAX_PUMP_RUNTIME_MS` for your tank's dimensions and typical fill time.
5. Upload and open Serial Monitor (115200 baud for ESP32, 9600 for UNO) to view live readings.

## Calibration

- Measure the distance from the sensor to the water surface when the tank is completely full → set as `FULL_DISTANCE`.
- Measure the distance when the tank is at your desired "low" trigger point → set as `LOW_DISTANCE`.
- Time a normal fill cycle and set `MAX_PUMP_RUNTIME_MS` a bit higher than that, so the safety cutoff only trips on an actual fault.

## License

MIT
# water-tank-auto-controller
