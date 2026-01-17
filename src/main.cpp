#include <Arduino.h>
#include <BleKeyboard.h>
#include <NimBLEDevice.h> 
#include <WiFi.h> 
#include <Preferences.h> 

// --- KONFIGURATION ---
BleKeyboard bleKeyboard("OneNote Switch", "DeinName", 100);
Preferences preferences;

// UUIDs
#define CONFIG_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CONFIG_CHAR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// PINS
const int buttonNextPin = 25; 
const int buttonPrevPin = 32; 
const int batteryPin = 36; 

const unsigned long SLEEP_TIMEOUT = 60000 * 5; 
unsigned long lastActivityTime = 0;
int pendingAction = 0;

// Modus-Variable
bool isConfigMode = false;

// Tasten-Variablen
uint8_t nextKeyMod = KEY_LEFT_CTRL;
uint8_t nextKeyCode = KEY_PAGE_DOWN;
uint8_t prevKeyMod = KEY_LEFT_CTRL;
uint8_t prevKeyCode = KEY_PAGE_UP;

// --- CALLBACK FÜR CONFIG ---
class ConfigCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            String command = String(value.c_str());
            Serial.println("Config Empfangen: " + command);
            
            if (command.startsWith("N:")) {
                int firstColon = command.indexOf(':');
                int secondColon = command.lastIndexOf(':');
                if (firstColon > 0 && secondColon > firstColon) {
                    nextKeyMod = command.substring(firstColon + 1, secondColon).toInt();
                    nextKeyCode = command.substring(secondColon + 1).toInt();
                    preferences.putUChar("modNext", nextKeyMod);
                    preferences.putUChar("codeNext", nextKeyCode);
                    Serial.println("NEXT gespeichert!");
                }
            }
            else if (command.startsWith("P:")) {
                int firstColon = command.indexOf(':');
                int secondColon = command.lastIndexOf(':');
                if (firstColon > 0 && secondColon > firstColon) {
                    prevKeyMod = command.substring(firstColon + 1, secondColon).toInt();
                    prevKeyCode = command.substring(secondColon + 1).toInt();
                    preferences.putUChar("modPrev", prevKeyMod);
                    preferences.putUChar("codePrev", prevKeyCode);
                    Serial.println("PREV gespeichert!");
                }
            }
            lastActivityTime = millis();
        }
    }
};

void debug(String msg) {
  Serial.println(msg);       
}

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
  debug("Gute Nacht! Gehe in Deep Sleep.");
  
  if (!isConfigMode) {
      bleKeyboard.end(); 
  }
  
  esp_sleep_enable_ext0_wakeup((gpio_num_t)buttonNextPin, 0); 
  esp_sleep_enable_ext1_wakeup((1ULL << buttonPrevPin), ESP_EXT1_WAKEUP_ALL_LOW);
  esp_deep_sleep_start();
}

void sendKey(uint8_t modifier, uint8_t key) {
  debug("Sende Taste...");
  if (modifier != 0) bleKeyboard.press(modifier);
  bleKeyboard.press(key);
  delay(50);
  bleKeyboard.releaseAll();
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);
  
  pinMode(buttonNextPin, INPUT_PULLUP);
  pinMode(buttonPrevPin, INPUT_PULLUP);
  analogReadResolution(12); 

  // Tasten laden
  preferences.begin("keyconf", false); 
  nextKeyMod = preferences.getUChar("modNext", KEY_LEFT_CTRL);   
  nextKeyCode = preferences.getUChar("codeNext", KEY_PAGE_DOWN); 
  prevKeyMod = preferences.getUChar("modPrev", KEY_LEFT_CTRL);   
  prevKeyCode = preferences.getUChar("codePrev", KEY_PAGE_UP);   

  // --- ENTSCHEIDUNG: CONFIG ODER NORMAL? ---
  if (digitalRead(buttonNextPin) == LOW) {
      delay(100); 
      if (digitalRead(buttonNextPin) == LOW) {
          isConfigMode = true;
      }
  }

  // Prüfen ob aus DeepSleep aufgewacht
  if (!isConfigMode) {
      esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
      if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) pendingAction = 1; 
      else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) pendingAction = 2; 
  }

  if (isConfigMode) {
      // +++ WARTUNGS-MODUS (KEIN KEYBOARD) +++
      debug("!!! START IN CONFIG MODE !!!");
      debug("Erstelle 'OneNote SETUP'...");

      NimBLEDevice::init("OneNote SETUP");
      
      // FIX: createServer() nutzen, da wir bleKeyboard.begin() nicht aufrufen!
      NimBLEServer* pServer = NimBLEDevice::createServer(); 
      
      NimBLEService* pService = pServer->createService(CONFIG_SERVICE_UUID);
      NimBLECharacteristic* pChar = pService->createCharacteristic(
                                       CONFIG_CHAR_UUID, 
                                       NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
                                    );
      pChar->setCallbacks(new ConfigCallbacks());
      pService->start();

      NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
      
      NimBLEAdvertisementData mainAd;
      mainAd.setFlags(0x06); 
      mainAd.setCompleteServices(NimBLEUUID(CONFIG_SERVICE_UUID));
      pAdvertising->setAdvertisementData(mainAd);

      NimBLEAdvertisementData scanResponse;
      scanResponse.setName("OneNote SETUP");
      pAdvertising->setScanResponseData(scanResponse);
      
      pAdvertising->start();
      
      debug("Bereit zum Verbinden via Browser!");

  } else {
      // +++ NORMALER MODUS (TASTATUR) +++
      debug("Normaler Start (Tastatur)");
      int startBatteryLevel = getBatteryPercentage();
      bleKeyboard.setBatteryLevel(startBatteryLevel, true); 
      bleKeyboard.begin();
  }
  
  lastActivityTime = millis();
}

void loop() {
  if (millis() - lastActivityTime >= SLEEP_TIMEOUT) {
    goToDeepSleep();
  }

  if (isConfigMode) {
      delay(100);
      return; 
  }

  if (bleKeyboard.isConnected()) {
    if (pendingAction != 0) {
       delay(500); 
       if (pendingAction == 1) sendKey(nextKeyMod, nextKeyCode);
       if (pendingAction == 2) sendKey(prevKeyMod, prevKeyCode);
       bleKeyboard.setBatteryLevel(getBatteryPercentage(), true); 
       pendingAction = 0;
       lastActivityTime = millis();
    }

    if(digitalRead(buttonNextPin) == LOW) {
      sendKey(nextKeyMod, nextKeyCode);
      bleKeyboard.setBatteryLevel(getBatteryPercentage(), true); 
      lastActivityTime = millis(); 
      delay(300); 
    }

    if(digitalRead(buttonPrevPin) == LOW) {
      sendKey(prevKeyMod, prevKeyCode);
      bleKeyboard.setBatteryLevel(getBatteryPercentage(), true);
      lastActivityTime = millis(); 
      delay(300); 
    }
  } 

  if (!bleKeyboard.isConnected()) {
      if (millis() - lastActivityTime > 120000) goToDeepSleep(); 
  }
}