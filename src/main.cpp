#include <Arduino.h>
#include <BleKeyboard.h>
#include <NimBLEDevice.h> 
#include <WiFi.h> 
#include <Preferences.h> 
#include <esp_system.h> 
#include <esp_mac.h> 

// --- UUIDs ---
#define CONFIG_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CONFIG_CHAR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// --- PINS ---
const int buttonNextPin = 25; 
const int buttonPrevPin = 32; 
const int batteryPin = 36; 
const int ledPin = 2; 

// --- GLOBALE OBJEKTE ---
// BleKeyboard nutzen wir NUR im Normalbetrieb
BleKeyboard* pKeyboard = nullptr;
Preferences preferences;

// --- EINSTELLUNGEN ---
// Standardwerte
uint8_t nextKeyMod = KEY_LEFT_CTRL;
uint8_t nextKeyCode = KEY_PAGE_DOWN;
uint8_t prevKeyMod = KEY_LEFT_CTRL;
uint8_t prevKeyCode = KEY_PAGE_UP;

// --- TIMING ---
const unsigned long SLEEP_TIMEOUT = 60000 * 5;     // 5 Min im Normalmodus
const unsigned long SETUP_TIMEOUT = 60000 * 10;    // 10 Min im Setupmodus
unsigned long lastActivityTime = 0;
int pendingAction = 0;
bool isConfigMode = false;

// --- CALLBACK FÜR CONFIG (Nur im Setup Modus aktiv) ---
class ConfigCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            String command = String(value.c_str());
            Serial.println("Config CMD: " + command);
            
            // Format N:Mod:Code oder P:Mod:Code
            if (command.startsWith("N:") || command.startsWith("P:")) {
                int firstDiv = command.indexOf(':');
                int secondDiv = command.lastIndexOf(':');
                
                if (firstDiv > 0 && secondDiv > firstDiv) {
                    uint8_t mod = command.substring(firstDiv + 1, secondDiv).toInt();
                    uint8_t code = command.substring(secondDiv + 1).toInt();
                    
                    if (command.startsWith("N:")) {
                        preferences.putUChar("modNext", mod);
                        preferences.putUChar("codeNext", code);
                        Serial.println("-> NEXT gespeichert");
                    } else {
                        preferences.putUChar("modPrev", mod);
                        preferences.putUChar("codePrev", code);
                        Serial.println("-> PREV gespeichert");
                    }
                }
            }
            lastActivityTime = millis(); // Wach bleiben
        }
    }
};

// --- HILFSFUNKTIONEN ---

int getBatteryPercentage() {
  long sum = 0;
  for(int i = 0; i < 10; i++) {
    sum += analogRead(batteryPin);
    delay(5);
  }
  int rawValue = sum / 10; 
  float voltage = (rawValue / 4095.0) * 3.3 * 2.43; 
  
  int percentage = 0;
  if (voltage >= 4.2) percentage = 100;
  else if (voltage <= 3.3) percentage = 0;
  else percentage = (int)((voltage - 3.3) / (4.2 - 3.3) * 100);
  
  if (percentage > 100) percentage = 100;
  return percentage;
}

void goToDeepSleep() {
  Serial.println("Gehe schlafen...");
  
  // Clean disconnect
  if (isConfigMode) {
      NimBLEDevice::deinit(true);
  } else if (pKeyboard) {
      pKeyboard->end();
  }
  
  // Wakeup konfigurieren
  esp_sleep_enable_ext0_wakeup((gpio_num_t)buttonNextPin, 0); 
  esp_sleep_enable_ext1_wakeup((1ULL << buttonPrevPin), ESP_EXT1_WAKEUP_ALL_LOW);
  
  digitalWrite(ledPin, LOW);
  esp_deep_sleep_start();
}

void setup() {
  // 1. Hardware Init
  Serial.begin(115200);
  pinMode(buttonNextPin, INPUT_PULLUP);
  pinMode(buttonPrevPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  analogReadResolution(12);
  
  // WICHTIG: WiFi aus für Stromsparen und sauberes BT
  WiFi.mode(WIFI_OFF); 

  Serial.println("\n--- BOOT ---");

  // 2. Modus Entscheidung (Taste gedrückt?)
  if (digitalRead(buttonNextPin) == LOW) {
      delay(500); 
      if (digitalRead(buttonNextPin) == LOW) {
          isConfigMode = true;
      }
  }

  // 3. Settings laden
  preferences.begin("keyconf", false); 
  nextKeyMod = preferences.getUChar("modNext", KEY_LEFT_CTRL);   
  nextKeyCode = preferences.getUChar("codeNext", KEY_PAGE_DOWN); 
  prevKeyMod = preferences.getUChar("modPrev", KEY_LEFT_CTRL);   
  prevKeyCode = preferences.getUChar("codePrev", KEY_PAGE_UP);   

  // 4. Wakeup Grund (Nur relevant für Normalbetrieb)
  if (!isConfigMode) {
      esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
      if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) pendingAction = 1; 
      else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) pendingAction = 2; 
  }

  // ---------------------------------------------------------
  // STARTLOGIK
  // ---------------------------------------------------------

  if (isConfigMode) {
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++
      // MODUS: SETUP (Clean NimBLE Server, No Bonding)
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++
      Serial.println(">> SETUP MODE AKTIV <<");
      
      // Feedback: Schnelles Blinken
      for(int i=0; i<5; i++) { digitalWrite(ledPin, HIGH); delay(100); digitalWrite(ledPin, LOW); delay(100); }

      // 1. MAC Ändern (Identity Separation)
      uint8_t mac[6];
      esp_read_mac(mac, ESP_MAC_BT);
      mac[5] += 4; // Wir verschieben die MAC deutlich
      esp_base_mac_addr_set(mac);
      Serial.printf("Setup MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

      // 2. NimBLE Init (Manuell, ohne BleKeyboard Helper!)
      NimBLEDevice::init("OneNote CONFIG");
      NimBLEDevice::setPower(ESP_PWR_LVL_P9);
      
      // 3. CRITICAL: Security deaktivieren! 
      NimBLEDevice::setSecurityAuth(false, false, false);

      NimBLEServer* pServer = NimBLEDevice::createServer();
      
      NimBLEService* pService = pServer->createService(CONFIG_SERVICE_UUID);
      NimBLECharacteristic* pChar = pService->createCharacteristic(
                                       CONFIG_CHAR_UUID,
                                       NIMBLE_PROPERTY::READ | 
                                       NIMBLE_PROPERTY::WRITE | 
                                       NIMBLE_PROPERTY::WRITE_NR
                                    );
      pChar->setCallbacks(new ConfigCallbacks());
      pService->start();

      // 4. Advertising
      NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
      pAdvertising->setAppearance(0); // 0 = Unknown (Kein Keyboard Icon!)
      pAdvertising->addServiceUUID(CONFIG_SERVICE_UUID);
      
      // FIX: setScanResponse(true) entfernt.
      // Stattdessen fügen wir explizit ScanResponse-Daten hinzu (Name).
      NimBLEAdvertisementData scanResponseData;
      scanResponseData.setName("OneNote CONFIG");
      pAdvertising->setScanResponseData(scanResponseData);
      
      pAdvertising->start();
      
      Serial.println("Ready. Warte auf Web-Verbindung...");

  } else {
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++
      // MODUS: NORMAL (HID Keyboard mit Bonding)
      // +++++++++++++++++++++++++++++++++++++++++++++++++++++
      Serial.println(">> NORMAL MODE (Tastatur) <<");
      
      // Hier nutzen wir die Komfort-Klasse
      pKeyboard = new BleKeyboard("OneNote Switch", "DeinName", 100);
      
      // Wir starten mit der originalen MAC Adresse (Hardware)
      pKeyboard->setBatteryLevel(getBatteryPercentage());
      pKeyboard->begin();
  }

  lastActivityTime = millis();
}

void loop() {
  unsigned long timeout = isConfigMode ? SETUP_TIMEOUT : SLEEP_TIMEOUT;
  
  if (millis() - lastActivityTime > timeout) {
      goToDeepSleep();
  }

  // --- CONFIG LOOP ---
  if (isConfigMode) {
      // Heartbeat LED
      static unsigned long lastBlink = 0;
      if (millis() - lastBlink > 1000) {
          lastBlink = millis();
          digitalWrite(ledPin, !digitalRead(ledPin));
      }
      delay(20);
      return; 
  }

  // --- KEYBOARD LOOP ---
  if (pKeyboard && pKeyboard->isConnected()) {
      
      // Pending Actions aus DeepSleep
      if (pendingAction != 0) {
          delay(300); // Kurz warten für Verbindungstabilität
          Serial.println("Führe Wakeup-Aktion aus...");
          
          if (pendingAction == 1) {
              if(nextKeyMod) pKeyboard->press(nextKeyMod);
              pKeyboard->press(nextKeyCode);
          } else {
              if(prevKeyMod) pKeyboard->press(prevKeyMod);
              pKeyboard->press(prevKeyCode);
          }
          delay(50);
          pKeyboard->releaseAll();
          
          pKeyboard->setBatteryLevel(getBatteryPercentage());
          pendingAction = 0;
          lastActivityTime = millis();
      }

      // Live Tasten
      if (digitalRead(buttonNextPin) == LOW) {
          Serial.println("Click Next");
          if(nextKeyMod) pKeyboard->press(nextKeyMod);
          pKeyboard->press(nextKeyCode);
          delay(50);
          pKeyboard->releaseAll();
          pKeyboard->setBatteryLevel(getBatteryPercentage());
          lastActivityTime = millis();
          delay(300);
      }

      if (digitalRead(buttonPrevPin) == LOW) {
          Serial.println("Click Prev");
          if(prevKeyMod) pKeyboard->press(prevKeyMod);
          pKeyboard->press(prevKeyCode);
          delay(50);
          pKeyboard->releaseAll();
          pKeyboard->setBatteryLevel(getBatteryPercentage());
          lastActivityTime = millis();
          delay(300);
      }
  } else {
      // Wenn im Normalmodus die Verbindung abreißt (z.B. PC aus),
      // gehen wir schneller schlafen
      if (millis() - lastActivityTime > 120000) { // 2 Minuten suchen
          goToDeepSleep();
      }
  }
}