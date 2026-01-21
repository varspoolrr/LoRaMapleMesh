# Project: Harter Farms LoRa Gateway ("The Bridge")
**Status:** Operational (Golden Master)  
**Date:** 2026-01-21  
**Firmware Version:** v21.0  

## 1. Overview
This device acts as a transparent bridge between the LoRa sensor mesh and the Home Assistant network. It listens for 915MHz LoRa packets and forwards them immediately via MQTT over Ethernet. It includes a fail-safe "Mailbox" buffer to prevent data loss during network interruptions.

## 2. Hardware Inventory
* **Core Module:** RAK4631 (nRF52840 + SX1262 LoRa)
* **Base Board:** RAK19007 / RAK5005-O
* **Ethernet Module:** RAK13800 (W5100S Chip)
* **Antenna:** 915MHz LoRa Antenna (US915)

## 3. Pin Configuration (CRITICAL)
*Note: This specific firmware handles a hardware conflict where the Ethernet module can jam the SPI bus if not sequenced correctly.*

### Ethernet (W5100S)
* **CS (Chip Select):** Pin 26
* **RST (Reset):** Pin 21
* **PWR (Power Enable):** Pin 34 (Must be HIGH to enable bus)

### LoRa Radio (SX1262)
* **Init Method:** Handled via `lora_rak4630_init()` (Manufacturer Native)
* **Internal Map:** CS=`42`, DIO1=`15/17`, RST=`38`, BUSY=`19`

## 4. Software Logic
1.  **Boot Sequence:**
    * **Silence Ethernet:** Power ON (Pin 34) -> Hold Reset (Pin 21) -> Wait.
    * **Init LoRa:** Call native RAK init -> Start RX Mode.
    * **Wake Ethernet:** Release Reset (Pin 21) -> Acquire IP.
2.  **Mailbox Buffer:**
    * Incoming LoRa messages are saved to a variable (`hasNewMessage`).
    * Main loop checks MQTT connection.
    * If disconnected, it reconnects *before* attempting to send.
    * Message is only cleared from buffer after successful MQTT Publish.

## 5. Interfaces
* **Web Dashboard:** `http://<DEVICE_IP>` (Port 80)
    * Displays: Uptime, MQTT Status, Packet Count, Last RSSI, Last Message.
* **MQTT Output:**
    * **Topic:** `lora/mesh/rx`
    * **Payload:** Raw JSON string from node.

## 6. Libraries Used
* `SX126x-RAK4630.h` (Native RAK Library)
* `SPI.h`
* `RAK13800_W5100S.h`
* `PubSubClient.h`
* `Adafruit_TinyUSB.h`
