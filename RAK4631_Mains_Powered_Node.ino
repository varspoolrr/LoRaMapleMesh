/*
   RAK4631 - HARTER FARMS BRIDGE (v21.0)
   -------------------------------------
   - BASE: v20.0 (Buffered Mailbox)
   - NEW: Internal Web Server (Port 80)
   - USAGE: Visit the IP address to see status.
*/

#include <Arduino.h>
#include <SX126x-RAK4630.h>
#include <SPI.h>
#include <RAK13800_W5100S.h>
#include <PubSubClient.h>
#include <Adafruit_TinyUSB.h>
#include "secrets.h"

// --- ETHERNET PINS ---
#define W5500_CS_PIN   26  
#define W5500_RST_PIN  21  
#define W5500_PWR_PIN  34  

// --- LORA CONFIG ---
#define RF_FREQUENCY 915000000  
#define TX_OUTPUT_POWER 22
#define LORA_BANDWIDTH 0        
#define LORA_SPREADING_FACTOR 7 
#define LORA_CODINGRATE 1       
#define LORA_PREAMBLE_LENGTH 8
#define LORA_SYMBOL_TIMEOUT 0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define RX_TIMEOUT_VALUE 3000

// --- GLOBALS ---
static RadioEvents_t RadioEvents;
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x05 }; 
String NODE_ID = "rak_gateway_01"; 

// Network Objects
EthernetClient ethClient;
PubSubClient client(ethClient);
EthernetServer server(80); // <--- WEB SERVER ON PORT 80

// --- STATE VARIABLES (For Dashboard) ---
volatile bool hasNewMessage = false; 
String incomingPayload = "";         
String lastMessageDisplay = "Waiting for data..."; // Saved for the website
int lastRssiDisplay = 0;
int msgCount = 0;
unsigned long lastMqttAttempt = 0;

// --- CALLBACKS ---
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void OnRxTimeout(void);
void OnRxError(void);

// ==========================================
//            SETUP
// ==========================================
void setup() {
  time_t timeout = millis();
  Serial.begin(115200);
  while (!Serial) {
    if ((millis() - timeout) < 5000) delay(100);
    else break;
  }
  
  Serial.println("\n=== HARTER FARMS BRIDGE (v21.0 Dashboard) ===");

  // 1. ETHERNET PREP
  pinMode(W5500_PWR_PIN, OUTPUT); digitalWrite(W5500_PWR_PIN, HIGH); delay(100); 
  pinMode(W5500_CS_PIN, OUTPUT); digitalWrite(W5500_CS_PIN, HIGH);
  pinMode(W5500_RST_PIN, OUTPUT); digitalWrite(W5500_RST_PIN, LOW); 
  delay(50);

  // 2. NATIVE LORA INIT
  lora_rak4630_init(); 
  
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.TxDone = NULL;
  RadioEvents.TxTimeout = NULL;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
            LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
            LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
            0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

  Serial.println("Radio Listening...");
  Radio.Rx(0);

  // 3. WAKE ETHERNET
  digitalWrite(W5500_RST_PIN, HIGH); 
  delay(500);

  Ethernet.init(W5500_CS_PIN);
  Ethernet.begin(mac);
  Serial.print("IP: "); Serial.println(Ethernet.localIP());

  // 4. START SERVICES
  server.begin(); // Start Web Server
  client.setServer(SECRET_MQTT_IP, SECRET_MQTT_PORT);
}

// ==========================================
//            WEB SERVER HANDLER
// ==========================================
void handleWebClient() {
  EthernetClient webClient = server.available();
  if (webClient) {
    // Standard HTTP Request Parser
    boolean currentLineIsBlank = true;
    while (webClient.connected()) {
      if (webClient.available()) {
        char c = webClient.read();
        
        // If we reached the end of the header (blank line), send response
        if (c == '\n' && currentLineIsBlank) {
          // --- HTTP HEADER ---
          webClient.println("HTTP/1.1 200 OK");
          webClient.println("Content-Type: text/html");
          webClient.println("Connection: close");
          webClient.println("Refresh: 5"); // Auto-refresh every 5 seconds
          webClient.println();
          
          // --- HTML BODY ---
          webClient.println("<!DOCTYPE HTML>");
          webClient.println("<html><head><style>");
          webClient.println("body { font-family: sans-serif; background: #f4f4f4; text-align: center; padding: 20px; }");
          webClient.println(".card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); max-width: 600px; margin: auto; }");
          webClient.println("h1 { color: #2c3e50; }");
          webClient.println(".stat { font-size: 1.2em; margin: 10px 0; color: #555; }");
          webClient.println(".data { font-weight: bold; color: #27ae60; }");
          webClient.println("</style></head><body>");
          
          webClient.println("<div class='card'>");
          webClient.println("<h1>Harter Farms Gateway</h1>");
          
          // System Stats
          webClient.print("<div class='stat'>Uptime: <span class='data'>");
          webClient.print(millis() / 1000);
          webClient.println(" sec</span></div>");

          webClient.print("<div class='stat'>MQTT Status: <span class='data' style='color:");
          webClient.print(client.connected() ? "#27ae60" : "#c0392b");
          webClient.print("'>");
          webClient.print(client.connected() ? "CONNECTED" : "DISCONNECTED");
          webClient.println("</span></div>");

          // LoRa Stats
          webClient.print("<div class='stat'>Packets Received: <span class='data'>");
          webClient.print(msgCount);
          webClient.println("</span></div>");

          webClient.print("<div class='stat'>Last RSSI: <span class='data'>");
          webClient.print(lastRssiDisplay);
          webClient.println(" dBm</span></div>");

          webClient.println("<div class='stat'>Last Message:</div>");
          webClient.print("<div style='background:#eee; padding:10px; border-radius:4px; font-family:monospace; word-wrap:break-word;'>");
          webClient.print(lastMessageDisplay);
          webClient.println("</div>");
          
          webClient.println("</div></body></html>");
          break;
        }
        
        if (c == '\n') currentLineIsBlank = true;
        else if (c != '\r') currentLineIsBlank = false;
      }
    }
    delay(1); // Give browser time to receive data
    webClient.stop(); // Close connection
  }
}

// ==========================================
//            LOOP
// ==========================================
void loop() {
  // 1. Radio IRQ
  Radio.IrqProcess(); 

  // 2. Network Maintenance
  Ethernet.maintain();
  
  // 3. WEB SERVER (Poll for visitors)
  handleWebClient();

  // 4. MAILBOX & MQTT
  if (hasNewMessage) {
    // Guaranteed Connection Logic
    if (!client.connected()) {
      Serial.print("MQTT Connecting... ");
      if (client.connect(NODE_ID.c_str(), SECRET_MQTT_USER, SECRET_MQTT_PASS)) {
        Serial.println("OK");
        client.publish("harter/gateway/status", "online");
      } else {
        Serial.println("Fail");
        return; // Try again next loop
      }
    }
    
    // Send
    if (client.publish("lora/mesh/rx", incomingPayload.c_str())) {
      Serial.println(" -> Sent to MQTT");
      hasNewMessage = false; 
      incomingPayload = "";
    }
  }

  // 5. MQTT Loop
  if (client.connected()) {
    client.loop();
  }
}

// ==========================================
//            CALLBACKS
// ==========================================
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  char message[size + 1];
  memcpy(message, payload, size);
  message[size] = '\0';
  
  // Update Dashboard Variables (Global)
  lastMessageDisplay = String(message);
  lastRssiDisplay = rssi;
  msgCount++;

  // Update Mailbox (For MQTT)
  incomingPayload = lastMessageDisplay;
  hasNewMessage = true;

  Serial.print("[RX] RSSI:"); Serial.print(rssi);
  Serial.println(" (Buffered)");

  Radio.Rx(0);
}

void OnRxTimeout(void) { Radio.Rx(0); }
void OnRxError(void) { Radio.Rx(0); }