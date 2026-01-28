/*
   HELTEC V4 - HARTER FARMS PRODUCTION
   FIRMWARE v1.3.0 (GOLDEN MASTER)
   -----------------------------------
   - FINAL BUILD FOR DEPLOYMENT
   - FEATURES:
     1. Auto-Recovery: Fixes JSN-SR04T Lock-ups.
     2. Power Mgmt: Zero-drain sleep (Phantom power kill).
     3. Bus Reset: Fixes "Sensors Not Found" on wake.
     4. Double Tap: Fixes DS18B20 "1.6F" glitch.
     5. Full Dashboard: Displays all 7 metrics on OLED.
*/

#include "LoRaWan_APP.h"
#include <WiFi.h>
#include <WebServer.h>
#include "HT_SSD1306Wire.h"
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h> 
#include <Wire.h>
#include "Adafruit_MPRLS.h" 
#include "secrets.h" 

#define FIRMWARE_VERSION "1.3.0" 
#define BUILD_DATE       "01.27.2026"  

// ==========================================
//            USER CONFIGURATION
// ==========================================
bool IS_GATEWAY = false;      
String NODE_ID = "node_1";    

// BATTERY CALIBRATION (User Meter: 1.46V=0%, 2.05V=100%)
float BATT_CALIBRATION = 1.0; 
const int BATT_MIN_V   = 146; 
const int BATT_MAX_V   = 205; 

const int TANK_EMPTY_CM = 100; 
const int TANK_FULL_CM  = 20;  

// ==========================================
//            PIN DEFINITIONS
// ==========================================
#define PIN_TRIG     45   
#define PIN_ECHO     46   

#define VAC_SDA_PIN  41        
#define VAC_SCL_PIN  42        
#define PIN_CHRG     6         
#define PIN_DONE     7         
#define PIN_EXT_BATT 5         
#define ONE_WIRE_BUS 4         
#define LED_PIN      35
#define PRG_BUTTON   0  
#define PIN_MOSFET   48   

// ==========================================
//            GLOBAL OBJECTS
// ==========================================
SSD1306Wire myDisplay(0x3C, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
WebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
TwoWire vacWire = TwoWire(1); 
Adafruit_MPRLS mpr = Adafruit_MPRLS(-1, -1);

// --- RADIO ---
#define RF_FREQUENCY           915000000 
#define TX_OUTPUT_POWER        22        
#define LORA_BANDWIDTH         0         
#define LORA_SPREADING_FACTOR  7         
#define LORA_CODINGRATE        1         
#define LORA_PREAMBLE_LENGTH   8         
#define LORA_SYMBOL_TIMEOUT    0         
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON   false
#define BUFFER_SIZE            255 

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];
static RadioEvents_t RadioEvents;
int16_t rssi, rxSize;
bool lora_idle = true;

// --- SENSORS ---
bool hasVacuumSensor = false;
float baseline_hPa = 1013.25; 
String lastMessage = "Booting...";
float currentTemp = 0.0;
float currentVac  = 0.0;
int   solarStatus = 0; 
int   tankPercent = 0;
int   currentBatt = 0;

unsigned long lastTempTime = 0;
const long tempInterval = 300000; 
bool isScreenOn = true;
unsigned long screenOnTime = 0;
const long screenTimeout = 60000; 

// ==========================================
//            FORWARD DECLARATIONS
// ==========================================
void OnTxDone( void );
void OnTxTimeout( void );
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );
void readSensorsAndSend();
void powerSensorON();
void powerSensorOFF();

// ==========================================
//            HELPER FUNCTIONS
// ==========================================

void powerSensorON() {
  pinMode(PIN_TRIG, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_MOSFET, LOW); 
}

void powerSensorOFF() {
  digitalWrite(PIN_MOSFET, HIGH); 
  pinMode(PIN_TRIG, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_ECHO, OUTPUT);
  digitalWrite(PIN_ECHO, LOW); 
}

int performPing() {
  long totalDuration = 0;
  int validReadings = 0;
  
  digitalWrite(PIN_TRIG, LOW); delay(20);

  for (int i = 0; i < 5; i++) {
    digitalWrite(PIN_TRIG, LOW); delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(20); 
    digitalWrite(PIN_TRIG, LOW);
    
    long duration = pulseIn(PIN_ECHO, HIGH, 35000); 

    if (duration > 0) {
      totalDuration += duration;
      validReadings++;
    } 
    delay(60); 
  }

  if (validReadings == 0) return 0; 

  long avgDuration = totalDuration / validReadings;
  int distanceCm = avgDuration * 0.034 / 2;
  return distanceCm;
}

int readTankLevel() {
  int result = performPing();
  
  // Auto-Recovery Logic
  if (result == 0) {
      Serial.println("  ! SENSOR STALL. REBOOTING...");
      powerSensorOFF();
      delay(1000); 
      powerSensorON(); 
      delay(2000); 
      
      // Re-init buses after reboot
      vacWire.begin(VAC_SDA_PIN, VAC_SCL_PIN);
      sensors.begin();

      result = performPing();
      if (result > 0) Serial.println("  ! RECOVERED.");
  }
  
  if (result > 0) {
     int pct = map(result, TANK_EMPTY_CM, TANK_FULL_CM, 0, 100);
     if (pct < 0) pct = 0; if (pct > 100) pct = 100;
     Serial.printf("  > Tank Level: %d%% (%dcm)\n", pct, result);
     return pct;
  }
  return tankPercent; 
}

int getBatteryPercent() {
  uint16_t raw = analogRead(PIN_EXT_BATT);
  float pinVoltage = (raw / 4095.0) * 3.3;
  float realVoltage = pinVoltage * BATT_CALIBRATION; 
  int pct = map((long)(realVoltage * 100), BATT_MIN_V, BATT_MAX_V, 0, 100);
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  Serial.printf("  > Batt: %d%% (%.3f V)\n", pct, realVoltage);
  return pct;
}

int readSolarStatus() {
  bool chrgActive = (digitalRead(PIN_CHRG) == LOW);
  bool doneActive = (digitalRead(PIN_DONE) == LOW);
  if (chrgActive) return 1; 
  if (doneActive) return 2; 
  return 0;                   
}

float readVacuum() {
  if (!hasVacuumSensor) return 0.0;
  
  float current_hPa = mpr.readPressure();
  float diff_hPa = current_hPa - baseline_hPa;
  float vac_inHg = diff_hPa * 0.02953;
  if (vac_inHg > 0.1) vac_inHg = 0.0;
  
  return vac_inHg;
}

void wakeScreen() {
  isScreenOn = true;
  screenOnTime = millis();
  myDisplay.displayOn();
}

void checkButtonAndScreen() {
  if (digitalRead(PRG_BUTTON) == LOW) {
    wakeScreen();
    myDisplay.clear();
    myDisplay.setFont(ArialMT_Plain_16);
    myDisplay.drawString(0, 20, "Manual Read");
    myDisplay.display();
    delay(200); 
    readSensorsAndSend();
    delay(1000); 
  }
  
  if (isScreenOn && (millis() - screenOnTime > screenTimeout)) {
    isScreenOn = false;
    myDisplay.displayOff();
  }
}

void updateDisplay(String status = "") {
  if (!isScreenOn) return; 

  myDisplay.clear();
  myDisplay.setFont(ArialMT_Plain_10);
  
  // Row 1: ID and Ver
  myDisplay.drawString(0, 0, NODE_ID);
  myDisplay.drawString(85, 0, "v1.3"); 

  // Row 2: Tank & Batt
  myDisplay.drawString(0, 13, "Tank: " + String(tankPercent) + "%");
  myDisplay.drawString(70, 13, "Batt: " + String(currentBatt) + "%");

  // Row 3: Temp & Vac (Large)
  myDisplay.setFont(ArialMT_Plain_16);
  myDisplay.drawString(0, 26, String(currentTemp, 1) + "F");
  myDisplay.drawString(70, 26, String(currentVac, 1));
  myDisplay.setFont(ArialMT_Plain_10);
  myDisplay.drawString(108, 26, "Hg");

  // Row 4: Solar & Uptime
  String solStr = "--";
  if (solarStatus == 1) solStr = "Chrg";
  if (solarStatus == 2) solStr = "Full";
  
  myDisplay.drawString(0, 44, "Sol: " + solStr);
  myDisplay.drawString(70, 44, "Up: " + String(millis()/1000/60) + "m");
  
  // Row 5: Status
  myDisplay.drawString(0, 54, lastMessage);
  myDisplay.display();
}

// ==========================================
//            SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  // 1. MANUAL VEXT POWER ON 
  pinMode(36, OUTPUT); 
  digitalWrite(36, LOW); 
  delay(100);
  
  // *** INITIAL POWER: OFF ***
  pinMode(PIN_MOSFET, OUTPUT);
  digitalWrite(PIN_MOSFET, HIGH); 
  
  // 2. HELTEC MCU INIT
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  // 3. SENSOR PINS
  pinMode(PIN_CHRG, INPUT_PULLUP);
  pinMode(PIN_DONE, INPUT_PULLUP);
  pinMode(PIN_TRIG, OUTPUT);
  digitalWrite(PIN_TRIG, LOW); 
  pinMode(PIN_ECHO, INPUT);
  pinMode(PRG_BUTTON, INPUT_PULLUP); 
  analogReadResolution(12);

  // 4. DISPLAY INIT
  myDisplay.init();
  wakeScreen();
  
  myDisplay.clear();
  myDisplay.setFont(ArialMT_Plain_16);
  myDisplay.drawString(0, 0, "Harter Farms");
  myDisplay.setFont(ArialMT_Plain_10);
  myDisplay.drawString(0, 25, "FW: " FIRMWARE_VERSION);
  myDisplay.drawString(0, 40, "Node: " + NODE_ID);
  myDisplay.display();
  delay(2000); 

  updateDisplay("Booting...");
  
  // 7. RADIO INIT 
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone = OnRxDone;
  Radio.Init( &RadioEvents );
  Radio.SetChannel( RF_FREQUENCY );
  Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                                 LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                                 LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                                 true, 0, 0, LORA_IQ_INVERSION_ON, 3000 );
  Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                                 LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                                 LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                                 0, true, 0, 0, LORA_IQ_INVERSION_ON, true );

  if (IS_GATEWAY) {
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
    unsigned long startWifi = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 10000) delay(100);
    if (WiFi.status() == WL_CONNECTED) client.setServer(SECRET_MQTT_IP, SECRET_MQTT_PORT);
    server.begin();
  } else WiFi.mode(WIFI_OFF);
  
  Radio.Rx(0); 
  updateDisplay("Ready");
  delay(1000); 
  readSensorsAndSend();
}

// ==========================================
//            LOOP
// ==========================================
void loop() {
  Radio.IrqProcess();
  checkButtonAndScreen(); 

  if (millis() - lastTempTime > tempInterval) {
    lastTempTime = millis();
    readSensorsAndSend();
  }

  if (IS_GATEWAY) {
    if (WiFi.status() == WL_CONNECTED) {
       if (!client.connected()) {
          if (client.connect(("HeltecGw-" + String(random(999))).c_str(), SECRET_MQTT_USER, SECRET_MQTT_PASS)) client.subscribe("lora/mesh/tx");
       }
       client.loop();
    }
    server.handleClient();
  }
}

// ==========================================
//            LOGIC FUNCTIONS
// ==========================================

void readSensorsAndSend() {
  if (!IS_GATEWAY && lora_idle == false) return; 

  Serial.println("\n--- Sensor Cycle ---");

  // 1. POWER UP
  powerSensorON(); 
  delay(2000); 

  // 2. RESTART BUSES
  Serial.print("  > Init Temp... ");
  sensors.begin(); 
  sensors.setWaitForConversion(true); 
  Serial.println("Done.");

  Serial.print("  > Init Vac... ");
  vacWire.begin(VAC_SDA_PIN, VAC_SCL_PIN); 
  if (mpr.begin(0x18, &vacWire)) {
      hasVacuumSensor = true;
      Serial.println("Found.");
  } else {
      hasVacuumSensor = false;
      Serial.println("Not Found.");
  }

  // 3. READ TEMP (DOUBLE TAP)
  sensors.requestTemperatures(); 
  delay(100); 
  sensors.requestTemperatures(); 
  float newTemp = sensors.getTempFByIndex(0);
  
  if (newTemp > -50 && newTemp < 180 && newTemp != 32.0 && newTemp != 1.6) {
      currentTemp = newTemp;
  }
  Serial.printf("  > Temp: %.1f F\n", currentTemp);
  
  // 4. READ VACUUM
  currentVac = readVacuum();
  
  // 5. READ TANK
  tankPercent = readTankLevel(); 

  // 6. POWER DOWN
  powerSensorOFF(); 

  // 7. SYSTEM INFO
  solarStatus = readSolarStatus();
  currentBatt = getBatteryPercent(); // Stored in Global for Display
  
  // 8. SEND
  JsonDocument doc;
  doc["id"]   = NODE_ID;
  doc["temp"] = serialized(String(currentTemp, 1));
  doc["batt"] = currentBatt;
  doc["vac"]  = serialized(String(currentVac, 1)); 
  doc["sol"]  = solarStatus;
  doc["lvl"]  = tankPercent;
  doc["up"]   = millis() / 1000;
  
  String jsonString;
  serializeJson(doc, jsonString);

  if (IS_GATEWAY) {
      if (client.connected()) client.publish("lora/mesh/rx", jsonString.c_str(), true); 
      lastMessage = "Gw Update";
      updateDisplay("Sens Upd");
  } 
  else {
      sprintf(txpacket, "%s", jsonString.c_str());
      Serial.printf("TX: %s\r\n", txpacket);
      Radio.Send( (uint8_t *)txpacket, strlen(txpacket) );
      lora_idle = false;
      lastMessage = "TX Sent";
      updateDisplay("Sending...");
  }
}

// ==========================================
//            RADIO CALLBACKS
// ==========================================
void OnTxDone( void ) { Serial.println("TX Done"); lora_idle = true; updateDisplay("Sent OK"); Radio.Rx(0); }
void OnTxTimeout( void ) { Serial.println("TX Timeout"); lora_idle = true; Radio.Rx(0); }
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr ) {
  Radio.Sleep();
  memcpy(rxpacket, payload, size ); rxpacket[size] = '\0';
  Serial.printf("RX: \"%s\" | RSSI: %d\r\n", rxpacket, rssi);
  lastMessage = "RX: " + String(rssi) + "dB";
  if (isScreenOn) { digitalWrite(LED_PIN, HIGH); delay(50); digitalWrite(LED_PIN, LOW); }
  if (IS_GATEWAY && client.connected()) { client.publish("lora/mesh/rx", rxpacket, true); updateDisplay("MQTT Pub"); } 
  else { updateDisplay("RX Data"); }
  lora_idle = true; Radio.Rx(0);
}
