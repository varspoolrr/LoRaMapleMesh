# Harter Farms LoRa Sensor Network
**Project Lead:** Tim Harter
**System Status:** Operational
**Last Updated:** January 2026

## 1. Project Motivation
This project was designed as a **cost-effective alternative** to expensive commercial maple production monitoring tools.

By utilizing off-the-shelf components (Heltec, RAKwireless) and open-source software (Home Assistant), this system achieves professional-grade monitoring of vacuum lines and tank levels at a fraction of the price of proprietary industrial solutions.

## 2. Key Features & Sensors
The system provides real-time telemetry for the following critical data points:
* **Vacuum Monitoring:** Continuous pressure monitoring for maple syrup lines.
* **Tank Depth:** Proximity sensors to measure sap tank levels.
* **Temperature:** Environmental monitoring for freeze/thaw cycles.
* **Power Management:** Battery charge levels and solar charging status.
* **Central Integration:** All data is aggregated into a single Home Assistant dashboard.

## 3. System Architecture
The system consists of three distinct layers:

    [Field Layer]              [Transport Layer]             [Application Layer]
    
    (Sensor Node 1)  --\
                        \      (915 MHz LoRa)
    (Sensor Node 2)  ---->    [The Gateway]    ---->    [Home Assistant]
                        /      (MQTT Bridge)              (Dashboard & Alerting)
    (Sensor Node 3)  --/       (Ethernet)

## 4. Component Directory
This project is divided into three sub-components. Please refer to the specific README files in each folder for pinouts, code, and flashing instructions.

### A. The Gateway ("The Bridge")
* **Role:** The central hub. Listens for radio packets and forwards them to the network.
* **Hardware:** RAK4631 Core + RAK13800 Ethernet.
* **Key Feature:** Includes a local Web Dashboard and message buffering.
* **Documentation:** See `RAK4631_Gateway_README.md`

### B. The Nodes ("Field Sensors")
* **Role:** Remote data collection.
* **Hardware:** **Heltec LoRa v4**.
* **Key Feature:** Low-power sleep cycle, solar charging, and JSON data packets.
* **Documentation:** See `Heltec_Production_Node_README.md`

### C. The Dashboard ("Home Assistant")
* **Role:** Data visualization and alerting.
* **Software:** Home Assistant + Mosquitto MQTT Broker.
* **Key Feature:** Custom entities for Temperature, Vacuum, and Tank Levels.
* **Documentation:** See `Home_Assistant_README.md`

## 5. Quick Reference: Data Protocol
All devices on this network speak a common language. The Gateway does not modify data; it passes the raw JSON from the Node directly to the Dashboard.

**Topic:** `lora/mesh/rx`

**Standard Payload:**

    {
      "id": "node_2",       // Unique Device Name
      "temp": 24.5,         // Temperature (C)
      "batt": 4.1,          // Battery Voltage
      "vac": 12.0,          // Vacuum (inHg)
      "sol": 0,             // Solar Input
      "lvl": 85,            // Tank Level (Proximity %)
      "up": 1200            // Uptime (Seconds)
    }

## 6. Maintenance & Troubleshooting

### Gateway Status Check
If data stops arriving, check the Gateway's local status page first.
* **URL:** `http://<GATEWAY_IP>`
* **Indicators:**
    * **MQTT Status:** Should be "CONNECTED".
    * **Packets Received:** Should be increasing.

### Node Debugging
If a specific node shows "0.0" data but is still checking in:
* The radio is working (heartbeat is good).
* The sensors are likely unpowered. Check that the `Vext` pin (Pin 36/37) is pulled LOW during the read cycle.
