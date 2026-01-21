# Project: Harter Farms Production Node
**Hardware:** Heltec WiFi LoRa 32 (V3/V4)  
**Firmware Version:** v1.0.6  
**Role:** Remote Sensor Node  

## 1. Overview
A battery-powered remote sensor node designed to monitor environmental conditions (Maple lines, tanks, weather). It wakes up on a set interval, reads sensors, transmits a JSON packet via LoRa, and returns to deep sleep.

## 2. Data Protocol
The node transmits a JSON payload over LoRa (915MHz).

**Payload Structure:**
```json
{
  "id": "node_2",       // Unique Device Name
  "temp": 0.0,          // Temperature (Celsius)
  "batt": 0,            // Battery Voltage/Percentage
  "vac": 0.0,           // Vacuum Pressure (inHg/PSI)
  "sol": 0,             // Solar Charging Status (mV or flag)
  "lvl": 0,             // Tank Level (0-100%)
  "up": 6600            // Uptime in seconds (Heartbeat)
}
