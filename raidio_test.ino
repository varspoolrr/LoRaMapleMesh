/*
   HELTEC V4 - OFFICIAL RANGE TESTER (SCREEN FIXED)
   ------------------------------------------------
   - Uses the Unlocked/Licensed Official Driver.
   - Shows RSSI on the OLED Screen.
   - Fixes the "Multiple Definition" crash by renaming the screen object.
*/

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h" 

// --- CONFIGURATION ---
bool IS_SENDER = false;  // <--- SET FALSE FOR RECEIVER

// --- LICENSE KEY ---
// PASTE YOUR UNIQUE BOARD LICENSE HERE
// Example: {0x77F4497A, 0xB358EB7D, 0x92AF160B, 0xD254F2BF}
uint32_t LICENSE[4] = {0x00000000, 0x00000000, 0x00000000, 0x00000000};

// --- DISPLAY SETTINGS ---
// We renamed "display" to "myDisplay" to fix the crash
SSD1306Wire  myDisplay(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// --- RADIO SETTINGS ---
#define RF_FREQUENCY                                915000000 
#define TX_OUTPUT_POWER                             20        
#define LORA_BANDWIDTH                              0         
#define LORA_SPREADING_FACTOR                       7         
#define LORA_CODINGRATE                             1         
#define LORA_PREAMBLE_LENGTH                        8         
#define LORA_SYMBOL_TIMEOUT                         0         
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false
#define RX_TIMEOUT_VALUE                            1000
#define BUFFER_SIZE                                 30 

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

static RadioEvents_t RadioEvents;
int16_t txNumber;
int16_t rssi, rxSize;
bool lora_idle = true;

// Callback function prototypes
void OnTxDone( void );
void OnTxTimeout( void );
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );

void setup() {
    Serial.begin(115200);

    // 1. MANUALLY TURN ON SCREEN POWER (VEXT)
    pinMode(36, OUTPUT);
    digitalWrite(36, LOW); 
    delay(50);

    // 2. START DISPLAY
    myDisplay.init();
    myDisplay.setFont(ArialMT_Plain_16);
    myDisplay.clear();
    myDisplay.drawString(0, 0, "Radio Init...");
    myDisplay.display();
    
    // 3. START RADIO SYSTEM
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
    
    txNumber = 0;
    rssi = 0;

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
                                   
    myDisplay.clear();
    if(IS_SENDER) {
      myDisplay.drawString(0, 0, "SENDER MODE");
      myDisplay.drawString(0, 20, "20dBm / SF7");
    } else {
      myDisplay.drawString(0, 0, "RECEIVER MODE");
      myDisplay.drawString(0, 20, "Waiting...");
    }
    myDisplay.display();
}

void loop() {
    // ================= SENDER LOGIC =================
    if (IS_SENDER) {
        if (lora_idle) {
            delay(1000); 
            txNumber++;
            sprintf(txpacket, "PING %d", txNumber);
            Serial.printf("Sending: \"%s\"\r\n", txpacket);
            
            // Show on Screen
            myDisplay.clear();
            myDisplay.setFont(ArialMT_Plain_24);
            myDisplay.drawString(0, 0, "PING");
            myDisplay.drawString(60, 0, String(txNumber));
            myDisplay.setFont(ArialMT_Plain_10);
            myDisplay.drawString(0, 40, "Sending...");
            myDisplay.display();
            
            Radio.Send( (uint8_t *)txpacket, strlen(txpacket) );
            lora_idle = false; 
        }
    }
    
    // ================= RECEIVER LOGIC =================
    else {
        if (lora_idle) {
            lora_idle = false;
            Radio.Rx(0); 
        }
    }

    Radio.IrqProcess();
}

// --- CALLBACKS ---

void OnTxDone( void ) {
    Serial.println("TX Done!");
    if(IS_SENDER) {
       myDisplay.setFont(ArialMT_Plain_10);
       myDisplay.setColor(BLACK); 
       myDisplay.fillRect(0, 40, 128, 20);
       myDisplay.setColor(WHITE);
       myDisplay.drawString(0, 40, "Sent!");
       myDisplay.display();
    }
    lora_idle = true;
}

void OnTxTimeout( void ) {
    Radio.Sleep( );
    Serial.println("TX Timeout!");
    lora_idle = true;
}

void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr ) {
    Radio.Sleep( );
    memcpy(rxpacket, payload, size );
    rxpacket[size] = '\0';
    
    Serial.printf("RX: \"%s\" | RSSI: %d | SNR: %d\r\n", rxpacket, rssi, snr);
    
    // UPDATE RECEIVER SCREEN
    myDisplay.clear();
    myDisplay.setFont(ArialMT_Plain_16);
    myDisplay.drawString(0, 0, "Signal Strength");
    
    myDisplay.setFont(ArialMT_Plain_24);
    myDisplay.drawString(0, 25, String(rssi) + " dBm");
    
    myDisplay.setFont(ArialMT_Plain_10);
    myDisplay.drawString(0, 53, "SNR: " + String(snr) + "  Count: " + String(txNumber));
    myDisplay.display();
    
    txNumber++; 
    lora_idle = true; 
}