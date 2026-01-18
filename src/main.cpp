#include <Arduino.h>
#include <NimBLEDevice.h>

// --- KONFIGURATION ---
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHAR_BUTTON_UUID    "12345678-1234-1234-1234-1234567890ac"
#define CHAR_BATTERY_UUID   "12345678-1234-1234-1234-1234567890ad"

// PINS (Deine Hardware Belegung)
const int buttonNextPin = 25; 
const int buttonPrevPin = 32; 
const int batteryPin = 36; 
const int ledPin = 2; 

// EINSTELLUNGEN
const int BATTERY_INTERVAL = 5000; // Alle 5 Sekunden aktualisieren

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pCharButton = nullptr;
NimBLECharacteristic* pCharBattery = nullptr;
bool deviceConnected = false;
bool forceBatteryUpdate = false;

// --- CALLBACKS ---
class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        deviceConnected = true;
        forceBatteryUpdate = true; // Sofortiges Update beim Verbinden
        Serial.println("Gerät verbunden!");
    };
    void onDisconnect(NimBLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Gerät getrennt -> Starte Advertising...");
        NimBLEDevice::startAdvertising();
    }
};

int getBatteryPercentage() {
  // Mehrere Messungen für Stabilität
  long sum = 0;
  for(int i = 0; i < 20; i++) {
    sum += analogRead(batteryPin);
    delay(1);
  }
  
  // Dein kalibrierter Faktor: 2.43
  float voltage = (sum / 20.0 / 4095.0) * 3.3 * 2.43; 
  int percentage = (int)((voltage - 3.3) / (4.2 - 3.3) * 100);
  
  return constrain(percentage, 0, 100);
}

void setup() {
  Serial.begin(115200);
  
  pinMode(buttonNextPin, INPUT_PULLUP);
  pinMode(buttonPrevPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  
  analogReadResolution(12);

  // NimBLE Init
  NimBLEDevice::init("OneNote Remote");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); 

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  pCharButton = pService->createCharacteristic(
                      CHAR_BUTTON_UUID,
                      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
                  );

  pCharBattery = pService->createCharacteristic(
                      CHAR_BATTERY_UUID,
                      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
                  );

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  
  NimBLEAdvertisementData scanResponseData;
  scanResponseData.setName("OneNote Remote"); 
  pAdvertising->setScanResponseData(scanResponseData);
  
  pAdvertising->start();
  
  Serial.println("ESP32 Bereit. Warte auf App...");
  digitalWrite(ledPin, HIGH); delay(200); digitalWrite(ledPin, LOW);
}

void loop() {
  if (deviceConnected) {
      // 1. Tastenabfrage
      if (digitalRead(buttonNextPin) == LOW) {
          uint8_t val = 1;
          pCharButton->setValue(&val, 1);
          pCharButton->notify();
          digitalWrite(ledPin, HIGH); delay(100); digitalWrite(ledPin, LOW);
          delay(200); 
      }
      
      if (digitalRead(buttonPrevPin) == LOW) {
          uint8_t val = 2;
          pCharButton->setValue(&val, 1);
          pCharButton->notify();
          digitalWrite(ledPin, HIGH); delay(100); digitalWrite(ledPin, LOW);
          delay(200); 
      }

      // 2. Akku Senden (Alle 5 Sekunden)
      static unsigned long lastBat = 0;
      if (forceBatteryUpdate || millis() - lastBat > BATTERY_INTERVAL) {
          lastBat = millis();
          forceBatteryUpdate = false; 
          
          uint8_t level = getBatteryPercentage();
          pCharBattery->setValue(&level, 1);
          pCharBattery->notify();
      }
  }
  
  delay(10); 
}