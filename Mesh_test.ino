/*
   - Dual I2C: OLED (17/18) + Vacuum (41/42)
   - Vacuum: Auto-Zero Calibration at boot (3.3V)
   - Tank: AJ-SR04M (3.3V wired to GPIO 45/46)
   - Solar: Seengreat Monitor (CHRG=6, DONE=7)
   - Battery: Voltage Divider on GPIO 5
   - Temp: DS18B20 on GPIO 4 (With 185F Glitch Fix)
   - POWER SAVING:
     1. CPU idles at 80MHz, boosts to 240MHz for sensor reads
     2. Screen sleeps after 60s (Press PRG to wake)
     3. LED only blinks if Screen is ON
     4. TX Interval 5 minutes
     5. Transmits IMMEDIATELY on boot
*/

#include <WiFi.h>
#include <WebServer.h>
#include <RadioLib.h>
#include "SSD1306Wire.h"
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h> 
#include <Wire.h>
#include "Adafruit_MPRLS.h" 

// ==========================================
//           USER CONFIGURATION
// ==========================================
bool IS_GATEWAY = true;      // <--- SET TO TRUE FOR THE ONE CONNECTED TO WIFI
String NODE_ID = "node_1";    // <--- CHANGE THIS FOR EACH NODE!

// --- BATTERY CALIBRATION ---
float BATT_CALIBRATION = 2.17; 

// --- TANK CONFIGURATION (Centimeters) ---
const int TANK_EMPTY_CM = 100; 
const int TANK_FULL_CM  = 20;  

// --- WIFI & MQTT CREDENTIALS ---
const char* WIFI_SSID = "SSID";
const char* WIFI_PASS = "PASSWORD";
const char* MQTT_SERVER = "MQTT_SERVER";
const int   MQTT_PORT   = 1883;
const char* MQTT_USER   = "MQTT_USER"; 
const char* MQTT_PASS   = "MQTT_PASSWORD"; 

#define FREQUENCY    915.0

// ==========================================
//           PIN DEFINITIONS
// ==========================================
// FIXED TANK PINS (To avoid USB Conflict on 19/20)
#define PIN_TRIG     45
#define PIN_ECHO     46

#define VAC_SDA_PIN  41       
#define VAC_SCL_PIN  42       
#define PIN_CHRG     6        
#define PIN_DONE     7        
#define PIN_EXT_BATT 5        
#define ONE_WIRE_BUS 4        
#define LED_PIN      35
#define OLED_SDA     17
#define OLED_SCL     18
#define OLED_RST     21
#define VEXT_PIN     36   
#define LORA_NSS     8
#define LORA_DIO1    14
#define LORA_RST     12
#define LORA_BUSY    13
#define PRG_BUTTON   0  

// ==========================================
//           GLOBAL OBJECTS
// ==========================================
SSD1306Wire display(0x3C, OLED_SDA, OLED_SCL);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
WebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
TwoWire vacWire = TwoWire(1); 
Adafruit_MPRLS mpr = Adafruit_MPRLS(-1, -1);

bool hasVacuumSensor = false;
float baseline_hPa = 1013.25; 
String lastMessage = "Booting...";
volatile bool packetReceived = false;
float currentTemp = 0.0;
float currentVac  = 0.0;
int   solarStatus = 0; 
int   tankPercent = 0;

// --- TIMING VARIABLES ---
unsigned long lastTempTime = 0;
// UPDATE INTERVAL: 300000 = 5 Minutes
const long tempInterval = 300000; 

// --- SCREEN SAVER VARIABLES ---
bool isScreenOn = true;
unsigned long screenOnTime = 0;
const long screenTimeout = 60000; 

// ==========================================
//           HELPER FUNCTIONS
// ==========================================
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag() { packetReceived = true; }

// --- TANK LEVEL LOGIC ---
int readTankLevel() {
  digitalWrite(PIN_TRIG, LOW); delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long duration = pulseIn(PIN_ECHO, HIGH, 30000);
  if (duration == 0) return tankPercent; 
  int distanceCm = duration * 0.034 / 2;
  int pct = map(distanceCm, TANK_EMPTY_CM, TANK_FULL_CM, 0, 100);
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return pct;
}

int getBatteryPercent() {
  uint16_t raw = analogRead(PIN_EXT_BATT);
  float pinVoltage = (raw / 4095.0) * 3.3;
  float realVoltage = pinVoltage * BATT_CALIBRATION; 
  int pct = map((long)(realVoltage * 100), 320, 420, 0, 100);
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return pct;
}

int readSolarStatus() {
  bool chrgActive = (digitalRead(PIN_CHRG) == LOW);
  bool doneActive = (digitalRead(PIN_DONE) == LOW);
  if (chrgActive) return 1; 
  if (doneActive) return 2; 
  return 0;                 
}

// --- SMART DISPLAY MANAGER ---
void wakeScreen() {
  isScreenOn = true;
  screenOnTime = millis();
  display.displayOn();
}

void checkScreenTimeout() {
  if (digitalRead(PRG_BUTTON) == LOW) {
    wakeScreen();
    delay(200); 
  }
  if (isScreenOn && (millis() - screenOnTime > screenTimeout)) {
    isScreenOn = false;
    display.displayOff();
  }
}

void updateDisplay(String status = "") {
  if (!isScreenOn) return; 

  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, NODE_ID);
  
  if (IS_GATEWAY) {
     String conn = client.connected() ? "MQTT: ON" : "MQTT: --";
     display.drawString(80, 0, conn);
  } else {
     String sol = (solarStatus == 1) ? "SUN" : (solarStatus == 2) ? "FULL" : "--";
     display.drawString(90, 0, sol);
  }

  display.drawString(0, 12, "Tnk: " + String(tankPercent) + "%");
  if (status != "") display.drawString(64, 12, status);

  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 25, String(currentTemp, 1) + "F");
  display.drawString(64, 25, String(currentVac, 1) + "Hg");
  
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 45, lastMessage);
  display.display();
}

void sendData(String msg) {
  Serial.print("TX LoRa: "); Serial.println(msg);
  radio.standby();
  radio.transmit(msg);
  radio.startReceive();
  if (IS_GATEWAY && client.connected()) {
    client.publish("lora/mesh/rx", msg.c_str(), true); 
  }
}

float readVacuum() {
  if (!hasVacuumSensor) return 0.0;
  float current_hPa = mpr.readPressure();
  float diff_hPa = current_hPa - baseline_hPa;
  float vac_inHg = diff_hPa * 0.02953;
  if (vac_inHg > 0.1) vac_inHg = 0.0;
  return vac_inHg;
}

void readSensorsAndSend() {
  // --- BOOST CPU FOR SENSOR READ ---
  setCpuFrequencyMhz(240); 
  
  sensors.requestTemperatures(); 
  float newTemp = sensors.getTempFByIndex(0);
  
  // FIX: Ignore the "185" error code (Power-on reset value)
  if (newTemp > 180.0) {
      // Do not update currentTemp, keep old value
  } else if (newTemp > -100) { 
      // Only update if value is realistic (above -100F)
      currentTemp = newTemp;
  }
  
  currentVac  = readVacuum();
  solarStatus = readSolarStatus();
  int battPct = getBatteryPercent();
  tankPercent = readTankLevel();

  // --- DROP CPU BACK TO IDLE ---
  setCpuFrequencyMhz(80);

  JsonDocument doc;
  doc["id"]   = NODE_ID;
  doc["temp"] = serialized(String(currentTemp, 1));
  doc["batt"] = battPct;
  doc["vac"]  = serialized(String(currentVac, 1)); 
  doc["sol"]  = solarStatus;
  doc["lvl"]  = tankPercent;
  doc["up"]   = millis() / 1000;
  
  String jsonString;
  serializeJson(doc, jsonString);

  lastMessage = "TX Bat:" + String(battPct) + "%";
  sendData(jsonString);
  updateDisplay("Sent");
}

void reconnect() {
  if (!client.connected()) {
    String clientId = "Heltec-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      client.subscribe("lora/mesh/tx");
      updateDisplay("MQTT OK");
    } else { delay(5000); }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  sendData("{\"cmd\":\"" + msg + "\"}");
}

void setup() {
  // --- START AT IDLE SPEED ---
  setCpuFrequencyMhz(80); 

  Serial.begin(115200);

  pinMode(VEXT_PIN, OUTPUT); digitalWrite(VEXT_PIN, LOW); delay(100);
  pinMode(OLED_RST, OUTPUT); digitalWrite(OLED_RST, LOW); delay(50); digitalWrite(OLED_RST, HIGH); delay(50);
  
  pinMode(PIN_CHRG, INPUT_PULLUP);
  pinMode(PIN_DONE, INPUT_PULLUP);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  
  pinMode(PRG_BUTTON, INPUT_PULLUP); 
  
  analogReadResolution(12);

  display.init(); display.flipScreenVertically();
  
  wakeScreen();
  updateDisplay("Booting...");

  Wire.begin(OLED_SDA, OLED_SCL);
  vacWire.begin(VAC_SDA_PIN, VAC_SCL_PIN);
  
  if (mpr.begin(0x18, &vacWire)) {
    hasVacuumSensor = true;
    display.drawString(0, 0, "Calibrating..."); display.display();
    float total = 0;
    for (int i=0; i<10; i++) { total += mpr.readPressure(); delay(100); }
    baseline_hPa = total / 10.0;
  } else { hasVacuumSensor = false; }

  // --- TEMP SENSOR FIX: FORCE DUMMY READ ---
  setCpuFrequencyMhz(240); // Boost for initialization
  sensors.begin();
  sensors.requestTemperatures(); // Ask for temp
  delay(750); // Wait for conversion
  sensors.getTempFByIndex(0); // Throw away the "185" result
  setCpuFrequencyMhz(80); // Drop back to idle

  // --- LORA SETUP (2dBm for testing, change to 22 later) ---
  int state = radio.begin(FREQUENCY, 125.0, 9, 7, 0x12, 22);
  if (state == RADIOLIB_ERR_NONE) {
    radio.setDio2AsRfSwitch(true);
    radio.setPacketReceivedAction(setFlag);
    radio.startReceive();
  }

  if (IS_GATEWAY) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    client.setServer(MQTT_SERVER, MQTT_PORT);
    client.setCallback(mqttCallback);
    server.begin();
  } else { WiFi.mode(WIFI_OFF); }
  
  updateDisplay("Ready");

  // --- FORCE IMMEDIATE TRANSMIT ---
  // Don't wait 5 minutes! Send data right now.
  readSensorsAndSend();
}

void loop() {
  checkScreenTimeout();

  if (millis() - lastTempTime > tempInterval) {
    lastTempTime = millis();
    readSensorsAndSend();
  }
  
  if (packetReceived) {
    packetReceived = false;
    String str;
    int state = radio.readData(str);
    if (state == RADIOLIB_ERR_NONE) {
      lastMessage = "RX: Data"; 
      
      if (isScreenOn) {
        pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
        delay(50); digitalWrite(LED_PIN, LOW);
      }
      
      if (IS_GATEWAY && client.connected()) {
        client.publish("lora/mesh/rx", str.c_str(), true);
      }
      updateDisplay("RX OK");
    }
    radio.startReceive();
  }
  
  if (IS_GATEWAY) {
    if (!client.connected()) reconnect();
    client.loop();
    server.handleClient();
  }
}