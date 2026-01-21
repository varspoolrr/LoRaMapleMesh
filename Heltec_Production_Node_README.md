# Project: Harter Farms Production Node
**Hardware:** Heltec WiFi LoRa 32 (V3/V4)
**Firmware Version:** v1.0.6
**Role:** Remote Sensor Node

## 1. Overview
A battery-powered remote sensor node designed to monitor environmental conditions (Maple lines, tanks, weather). It wakes up on a set interval, reads sensors, transmits a JSON packet via LoRa, and returns to deep sleep.

## 2. Data Protocol
The node transmits a JSON payload over LoRa (915MHz).

**Payload Structure:**

{
  "id": "node_2",       // Unique Device Name
  "temp": 0.0,          // Temperature (Celsius)
  "batt": 0,            // Battery Voltage/Percentage
  "vac": 0.0,           // Vacuum Pressure (inHg/PSI)
  "sol": 0,             // Solar Charging Status (mV or flag)
  "lvl": 0,             // Tank Level (0-100%)
  "up": 6600            // Uptime in seconds (Heartbeat)
}

## 3. Hardware & Pinout (Heltec V3 Standard)
* **Processor:** ESP32-S3
* **Radio:** SX1262
* **LoRa NSS (CS):** Pin 8
* **LoRa RESET:** Pin 12
* **LoRa BUSY:** Pin 13
* **LoRa DIO1:** Pin 14
* **I2C SDA:** Pin 17
* **I2C SCL:** Pin 18
* **Battery Read Pin:** Pin 1 (Internal Divider)
* **Battery Control Pin (Vext):** Pin 37 (Must be pulled LOW to read battery/sensors)

## 4. Known Issues (v1.0.6)
* **"Zero Data" Bug:** Gateway logs show node_2 sending 0.0 for temperature, vacuum, and battery.
* **Likely Cause:** The Vext pin (Pin 37) is likely not being pulled LOW during the sensor read cycle. This pin controls the power transistor that feeds the external sensors and the battery voltage divider. If it is not LOW, the sensors are off.

## 5. Operational Cycle
1.  **Wake:** Timer-based wake up.
2.  **Read:** Power up sensors (Vext LOW) -> Delay -> Read values.
3.  **Transmit:** Send JSON packet via LoRa (Sync Word: Private).
4.  **Sleep:** Power down sensors (Vext HIGH) -> ESP32 Deep Sleep.
