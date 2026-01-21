/*
   HELTEC V4 - HARTER FARMS PRODUCTION
   FIRMWARE v1.0.6
   -----------------------------------
   - FEATURE: PRG Button now triggers instant sensor read & transmit.
   - FIX: Gateway continuously reads/reports local sensors to MQTT.
   - CLEAN: No license code included (relies on secrets.h).
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

// ==========================================
//           VERSION CONTROL
// ==========================================
#define FIRMWARE_VERSION "1.0.6" // <--- Version Bump
#define BUILD_DATE       "01.20.2026"  

// ==========================================
//           USER CONFIGURATION
// ==========================================
bool IS_GATEWAY = false;      // <--- SET TRUE FOR HOUSE UNIT
String NODE_ID = "node_4";    // <--- CHANGE FOR EACH NODE

// --- SENSOR CONFIG ---
float BATT_CALIBRATION = 2.17; 
const int TANK_EMPTY_CM = 100; 
const int TANK_FULL_CM  = 20;  

// ==========================================
//           PIN DEFINITIONS
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

// ==========================================
//           GLOBAL OBJECTS
// ==========================================
SSD1306Wire myDisplay(0x3C, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

WebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
TwoWire vacWire = TwoWire(1); 
Adafruit_MPRLS mpr = Adafruit_MPRLS(-1, -1);

// --- RADIO VARIABLES ---
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

// --- SENSOR VARIABLES ---
bool hasVacuumSensor = false;
float baseline_hPa = 1013.25; 
String lastMessage = "Booting...";
float currentTemp = 0.0;
float currentVac  = 0.0;
int   solarStatus = 0; 
int   tankPercent = 0;

unsigned long lastTempTime = 0;
const long tempInterval = 300000; // 5 Minutes
bool isScreenOn = true;
unsigned long screenOnTime = 0;
const long screenTimeout = 60000; 

// ==========================================
//           FORWARD DECLARATIONS
// ==========================================
void OnTxDone( void );
void OnTxTimeout( void );
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );
void readSensorsAndSend();

// ==========================================
//           HELPER FUNCTIONS
// ==========================================

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
  // If button is pressed (LOW)
  if (digitalRead(PRG_BUTTON) == LOW) {
    wakeScreen();
    
    // *** INSTANT TRANSMIT FEATURE ***
    // 1. Show feedback
    myDisplay.clear();
    myDisplay.setFont(ArialMT_Plain_16);
    myDisplay.drawString(0, 20, "Manual Read");
    myDisplay.display();
    delay(200); // Short delay to see message
    
    // 2. Force Read & Send
    readSensorsAndSend();
    
    // 3. Long debounce to prevent machine-gunning if held
    delay(1000); 
  }
  
  // Handle Screen Timeout
  if (isScreenOn && (millis() - screenOnTime > screenTimeout)) {
    isScreenOn = false;
    myDisplay.displayOff();
  }
}

void updateDisplay(String status = "") {
  if (!isScreenOn) return; 

  myDisplay.clear();
  myDisplay.setFont(ArialMT_Plain_10);
  
  myDisplay.drawString(0, 0, NODE_ID);
  myDisplay.drawString(80, 0, "v" FIRMWARE_VERSION); 
  
  if (IS_GATEWAY) {
      String conn = client.connected() ? "MQTT: ON" : "MQTT: --";
      myDisplay.drawString(40, 0, conn);
  }

  myDisplay.drawString(0, 12, "Tnk: " + String(tankPercent) + "%");
  if (status != "") myDisplay.drawString(64, 12, status);

  myDisplay.setFont(ArialMT_Plain_16);
  myDisplay.drawString(0, 25, String(currentTemp, 1) + "F");
  myDisplay.drawString(64, 25, String(currentVac, 1) + "Hg");
  
  myDisplay.setFont(ArialMT_Plain_10);
  myDisplay.drawString(0, 45, lastMessage);
  myDisplay.display();
}

// ==========================================
//           SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  // 1. MANUAL VEXT POWER ON 
  pinMode(36, OUTPUT); 
  digitalWrite(36, LOW); 
  delay(100);

  // 2. HELTEC MCU INIT
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  // 3. SENSOR PINS
  pinMode(PIN_CHRG, INPUT_PULLUP);
  pinMode(PIN_DONE, INPUT_PULLUP);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PRG_BUTTON, INPUT_PULLUP); 
  analogReadResolution(12);

  // 4. DISPLAY INIT
  myDisplay.init();
  // myDisplay.flipScreenVertically(); // Uncomment if upside down
  wakeScreen();
  
  myDisplay.clear();
  myDisplay.setFont(ArialMT_Plain_16);
  myDisplay.drawString(0, 0, "Harter Farms");
  myDisplay.setFont(ArialMT_Plain_10);
  myDisplay.drawString(0, 25, "FW: " FIRMWARE_VERSION);
  myDisplay.drawString(0, 40, "Built: " BUILD_DATE);
  myDisplay.display();
  delay(2000); 

  updateDisplay("Booting...");

  // 5. I2C SENSORS
  Wire.begin(SDA_OLED, SCL_OLED); 
  vacWire.begin(VAC_SDA_PIN, VAC_SCL_PIN);
  
  if (mpr.begin(0x18, &vacWire)) {
    hasVacuumSensor = true;
    myDisplay.drawString(0, 0, "Calibrating..."); myDisplay.display();
    float total = 0;
    for (int i=0; i<10; i++) { total += mpr.readPressure(); delay(50); }
    baseline_hPa = total / 10.0;
  } else { hasVacuumSensor = false; }

  // 6. TEMP SENSOR
  sensors.begin();

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

  // 8. GATEWAY WIFI SETUP
  if (IS_GATEWAY) {
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
    unsigned long startWifi = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 10000) { delay(100); }
    
    if (WiFi.status() == WL_CONNECTED) {
       client.setServer(SECRET_MQTT_IP, SECRET_MQTT_PORT);
       updateDisplay("WiFi OK");
    } else {
       updateDisplay("WiFi Fail");
    }
    server.begin();
  } else {
    WiFi.mode(WIFI_OFF);
  }
  
  Radio.Rx(0); 
  
  updateDisplay("Ready");
  // Force immediate initial read
  delay(1000); 
  readSensorsAndSend();
}

// ==========================================
//           LOOP
// ==========================================
void loop() {
  Radio.IrqProcess();
  
  // NEW: Checks button for Instant Transmit
  checkButtonAndScreen(); 

  // Regular Timer
  if (millis() - lastTempTime > tempInterval) {
    lastTempTime = millis();
    readSensorsAndSend();
  }

  if (IS_GATEWAY) {
    if (WiFi.status() == WL_CONNECTED) {
       if (!client.connected()) {
          if (client.connect(("HeltecGw-" + String(random(999))).c_str(), SECRET_MQTT_USER, SECRET_MQTT_PASS)) {
             client.subscribe("lora/mesh/tx");
          }
       }
       client.loop();
    }
    server.handleClient();
  }
}

// ==========================================
//           LOGIC FUNCTIONS
// ==========================================

void readSensorsAndSend() {
  // If Node: Wait for Idle. If Gateway: Send anyway (MQTT)
  if (!IS_GATEWAY && lora_idle == false) return; 

  sensors.requestTemperatures(); 
  float newTemp = sensors.getTempFByIndex(0);
  if (newTemp > -100 && newTemp < 180) currentTemp = newTemp;
  
  currentVac  = readVacuum();
  solarStatus = readSolarStatus();
  int battPct = getBatteryPercent();
  tankPercent = readTankLevel();

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

  if (IS_GATEWAY) {
      if (client.connected()) {
          client.publish("lora/mesh/rx", jsonString.c_str(), true); 
      }
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
//           RADIO CALLBACKS
// ==========================================
void OnTxDone( void ) {
  Serial.println("TX Done");
  lora_idle = true;
  updateDisplay("Sent OK");
  Radio.Rx(0);
}

void OnTxTimeout( void ) {
  Serial.println("TX Timeout");
  lora_idle = true;
  Radio.Rx(0);
}

void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr ) {
  Radio.Sleep();
  memcpy(rxpacket, payload, size );
  rxpacket[size] = '\0';
  
  Serial.printf("RX: \"%s\" | RSSI: %d\r\n", rxpacket, rssi);
  lastMessage = "RX: " + String(rssi) + "dB";
  
  if (isScreenOn) {
     digitalWrite(LED_PIN, HIGH); delay(50); digitalWrite(LED_PIN, LOW);
  }

  if (IS_GATEWAY && client.connected()) {
     client.publish("lora/mesh/rx", rxpacket, true);
     updateDisplay("MQTT Pub");
  } else {
     updateDisplay("RX Data");
  }

  lora_idle = true;
  Radio.Rx(0);
}
