#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "keys.h"



// Definición de las clases servidor
BLEServer *pServer;
BLEAdvertising *pAdvertising;

bool clientConnected = false;
bool oldDeviceConnected = false;
bool authenticated = false;
// bool lockedStatus = true;
// bool lastNotifiedStatus = !lockedStatus;

struct Status {
  bool locked = true;
  bool lastNotified = !locked;
};

Status status = {};

BLEService *pService;
BLECharacteristic *statusCharacteristic;

#define SERVICE_UUID "e6067851-5971-4b21-a8cc-17738c56ea49"
#define CHARACTERISTIC_UUID_STATUS "db9ab4aa-da20-4de8-8a08-e14ab7e5148e"
#define CHARACTERISTIC_UUID_RX "f0bf0a71-0dfa-4d1b-90ae-cfda669a37c0"


// Pass key, aun no implementado
uint32_t passkey = 123456;


#define LEDPIN 8
#define RELEPIN 10
#define BUZZZER 0

void tono() {
  noTone(BUZZZER);
  tone(BUZZZER, 1000);
  delay(200);
  noTone(BUZZZER);
  delay(5);
  tone(BUZZZER, 500);
  delay(200);
  noTone(BUZZZER);
}

void lock() {
  status.locked = true;
  digitalWrite(LEDPIN, 1);
  digitalWrite(RELEPIN, 0);
  tono();
}

void unlock() {
  status.locked = false;
  digitalWrite(LEDPIN, 0);
  digitalWrite(RELEPIN, 1);
  tono();
}


class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    clientConnected = true;
  }

  void onDisconnect(BLEServer *pServer) {
    clientConnected = false;
    lock();
  }
};

class rxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    // Obtener los bytes crudos (encriptados)
    String rxValue = pCharacteristic->getValue();

    if (rxValue.length() > 0) {
      // Decrypt
      unsigned char decrypted[INPUT_LENGTH + 1];
      memset(decrypted, 0, sizeof(decrypted));

      decrypt((unsigned char *)rxValue.c_str(), key, decrypted);

      String decryptedStr = String((char *)decrypted);

      if (decryptedStr == "1") {
        if (status.locked) {
          unlock();
        } else {
          lock();
        }
      }
    }
  }
};

void setup() {
  BLEDevice::init("ESP32-C3");

  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  pSecurity->setCapability(ESP_IO_CAP_NONE);  // ← Sin capacidad de PIN
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  pService = pServer->createService(SERVICE_UUID);
  statusCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_STATUS,
    BLECharacteristic::PROPERTY_NOTIFY);
  statusCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new rxCallbacks());
  pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);

  pService->start();

  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  pinMode(LEDPIN, OUTPUT);
  pinMode(RELEPIN, OUTPUT);
  pinMode(BUZZZER, OUTPUT);
  digitalWrite(LEDPIN, 1);
  digitalWrite(RELEPIN, 0);
}

void loop() {
  if (clientConnected) {

    // Solo notifica si el estado cambió
    if (status.locked != status.lastNotified) {
      char message[8];
      snprintf(message, sizeof(message), "%d,%d", status.locked ? 1 : 0, status.lastNotified ? 1 : 0);

      uint8_t messageBytes[32];  // Suficiente para "Moto bloqueada." + padding
      int outputLen = 0;

      encrypt(message, key, messageBytes, &outputLen);
      statusCharacteristic->setValue(messageBytes, outputLen);

      statusCharacteristic->notify();
      status.lastNotified = status.locked;  // Actualiza el último estado enviado
    }

  } else {
    // Parpadeo LED
    digitalWrite(LEDPIN, 0);
    delay(500);
    digitalWrite(LEDPIN, 1);
    delay(500);
  }

  if (!clientConnected && oldDeviceConnected) {
    pServer->startAdvertising();
    oldDeviceConnected = clientConnected;
    status.lastNotified = !status.locked;  // Reset para que notifique al reconectarse
    digitalWrite(LEDPIN, 0);
    delay(250);
    digitalWrite(LEDPIN, 1);
    delay(250);
  }

  if (clientConnected && !oldDeviceConnected) {
    oldDeviceConnected = clientConnected;
  }
}