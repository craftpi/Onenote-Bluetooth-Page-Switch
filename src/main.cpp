#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h> 

// --- KONFIGURATION ---
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHAR_BUTTON_UUID    "12345678-1234-1234-1234-1234567890ac"
#define CHAR_BATTERY_UUID   "12345678-1234-1234-1234-1234567890ad"

// PINS
const int buttonNextPin = 25; 
const int buttonPrevPin = 32; 
const int batteryPin = 36; 
const int ledPin = 2; 

// EINSTELLUNGEN
const int BATTERY_INTERVAL = 5000; 

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pCharButton = nullptr;
NimBLECharacteristic* pCharBattery = nullptr;
bool deviceConnected = false;
bool forceBatteryUpdate = false;

// --- CALLBACKS ---
class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        deviceConnected = true;
        forceBatteryUpdate = true;
        Serial.println("Gerät verbunden!");
    };
    void onDisconnect(NimBLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Gerät getrennt -> Starte Advertising...");
        NimBLEDevice::startAdvertising();
    }
};

int getBatteryPercentage() {
  long sum = 0;
  for(int i = 0; i < 20; i++) {
    sum += analogRead(batteryPin);
    delay(1);
  }
  float voltage = (sum / 20.0 / 4095.0) * 3.3 * 2.43; 
  int percentage = (int)((voltage - 3.3) / (4.2 - 3.3) * 100);
  return constrain(percentage, 0, 100);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);
  
  // WICHTIG: Wir bleiben bei INPUT_PULLUP für Stabilität,
  // aber wir passen die Abfrage-Logik unten an.
  pinMode(buttonNextPin, INPUT_PULLUP);
  pinMode(buttonPrevPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  
  // LED Logik für D1 Mini: HIGH = AUS, LOW = AN
  digitalWrite(ledPin, HIGH); 

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
  
  // Start-Animation
  for(int i=0; i<3; i++) { 
      digitalWrite(ledPin, LOW); delay(100); 
      digitalWrite(ledPin, HIGH); delay(100); 
  }
}

void loop() {
  // --- LOGIK ANPASSUNG ---
  // Dein Test hat gezeigt: Drücken = LED AUS (HIGH am Pin bei Standard LED Logik??)
  // Das ist verwirrend. Machen wir es sicher:
  // Wir prüfen auf LOW (GND Verbindung). Das ist Standard bei Buttons.
  
  bool btnNext = (digitalRead(buttonNextPin) == LOW);
  bool btnPrev = (digitalRead(buttonPrevPin) == LOW);

  // --- LED FEEDBACK ---
  // Wir wollen: Drücken -> LED AN (LOW)
  if (btnNext || btnPrev) {
      digitalWrite(ledPin, LOW); // LED AN
  } else {
      digitalWrite(ledPin, HIGH); // LED AUS
  }

  // --- BLUETOOTH LOGIK ---
  if (deviceConnected) {
      if (btnNext) {
          Serial.println("Sende: NEXT");
          uint8_t val = 1; 
          pCharButton->setValue(&val, 1);
          pCharButton->notify();
          delay(250); // Entprellen
      }
      
      if (btnPrev) {
          Serial.println("Sende: PREV");
          uint8_t val = 2; 
          pCharButton->setValue(&val, 1);
          pCharButton->notify();
          delay(250); // Entprellen
      }

      // Akku Senden
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