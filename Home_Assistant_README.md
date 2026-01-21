# Project: Harter Farms Home Assistant Integration
**Integration Type:** MQTT  
**Status:** Operational  

## 1. Prerequisites
* **MQTT Broker:** Mosquitto (or similar) must be installed and running.
* **HA Integration:** The "MQTT" integration must be added in Home Assistant settings.
* **Gateway Topic:** The LoRa Gateway must be publishing to `lora/mesh/rx`.

## 2. Configuration.yaml Setup
Add the following block to your `configuration.yaml` file. This listens to the general LoRa topic and filters messages specifically for `node_2`.

    mqtt:
      sensor:
        # --- HARTER FARMS: NODE 2 (Production) ---
        
        # 1. Temperature
        - name: "Node 2 Temperature"
          unique_id: "node_2_temp"
          state_topic: "lora/mesh/rx"
          value_template: >
            {% if value_json.id == 'node_2' %}
              {{ value_json.temp }}
            {% else %}
              {{ states('sensor.node_2_temperature') }}
            {% endif %}
          unit_of_measurement: "°C"
          device_class: temperature
          state_class: measurement

        # 2. Battery Level
        - name: "Node 2 Battery"
          unique_id: "node_2_batt"
          state_topic: "lora/mesh/rx"
          value_template: >
            {% if value_json.id == 'node_2' %}
              {{ value_json.batt }}
            {% else %}
              {{ states('sensor.node_2_battery') }}
            {% endif %}
          unit_of_measurement: "%"
          device_class: battery
          state_class: measurement

        # 3. Vacuum Pressure (Maple Lines)
        - name: "Node 2 Vacuum"
          unique_id: "node_2_vac"
          state_topic: "lora/mesh/rx"
          value_template: >
            {% if value_json.id == 'node_2' %}
              {{ value_json.vac }}
            {% else %}
              {{ states('sensor.node_2_vacuum') }}
            {% endif %}
          unit_of_measurement: "inHg"
          icon: mdi:gauge

        # 4. Tank Level
        - name: "Node 2 Tank Level"
          unique_id: "node_2_lvl"
          state_topic: "lora/mesh/rx"
          value_template: >
            {% if value_json.id == 'node_2' %}
              {{ value_json.lvl }}
            {% else %}
              {{ states('sensor.node_2_tank_level') }}
            {% endif %}
          unit_of_measurement: "%"
          icon: mdi:cup-water

        # 5. Solar Status
        - name: "Node 2 Solar"
          unique_id: "node_2_sol"
          state_topic: "lora/mesh/rx"
          value_template: >
            {% if value_json.id == 'node_2' %}
              {{ value_json.sol }}
            {% else %}
              {{ states('sensor.node_2_solar') }}
            {% endif %}
          unit_of_measurement: "V"
          device_class: voltage

        # 6. Uptime (Heartbeat)
        - name: "Node 2 Uptime"
          unique_id: "node_2_uptime"
          state_topic: "lora/mesh/rx"
          value_template: >
            {% if value_json.id == 'node_2' %}
              {{ value_json.up }}
            {% else %}
              {{ states('sensor.node_2_uptime') }}
            {% endif %}
          unit_of_measurement: "s"
          icon: mdi:clock-outline

## 3. Dashboard Configuration (Lovelace)
To display these sensors in a card, use the following YAML code in your Dashboard:

    type: entities
    title: Harter Farms - Node 2
    show_header_toggle: false
    entities:
      - entity: sensor.node_2_temperature
        name: Temperature
      - entity: sensor.node_2_vacuum
        name: Line Vacuum
      - entity: sensor.node_2_tank_level
        name: Tank Level
      - entity: sensor.node_2_battery
        name: Battery
      - entity: sensor.node_2_solar
        name: Solar Input
      - entity: sensor.node_2_uptime
        name: Uptime
        secondary_info: last-updated
