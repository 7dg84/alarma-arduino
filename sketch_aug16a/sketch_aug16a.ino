#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

#include "aes.h"
#include "keys.h"
#include "board_config.h"

// ---------------------------------------------------------------------------
// UUIDs (deben coincidir con BleConstants.kt de la app Android)
// ---------------------------------------------------------------------------
#define SERVICE_UUID              "e6067851-5971-4b21-a8cc-17738c56ea49"
#define CHARACTERISTIC_UUID_STATUS "db9ab4aa-da20-4de8-8a08-e14ab7e5148e"
#define CHARACTERISTIC_UUID_RX     "f0bf0a71-0dfa-4d1b-90ae-cfda669a37c0"

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
struct AlarmState {
  bool locked = true;
  bool lastNotified = false;
};

AlarmState alarmState;

BLEServer *pServer = nullptr;
BLECharacteristic *statusCharacteristic = nullptr;

#if defined(CONFIG_BLUEDROID_ENABLED)
BLE2902 *statusCccd = nullptr;
#endif

volatile bool clientConnected = false;
volatile bool notificationsEnabled = false;
volatile bool restartAdvertising = false;

enum PendingAction : uint8_t {
  ACTION_NONE = 0,
  ACTION_LOCK,
  ACTION_UNLOCK,
  ACTION_TOGGLE
};

volatile PendingAction pendingAction = ACTION_NONE;

unsigned long lastBlinkMs = 0;
bool blinkState = false;

// ---------------------------------------------------------------------------
// Salidas
// ---------------------------------------------------------------------------
void setLed(bool on) {
  digitalWrite(LEDPIN, on ? HIGH : LOW);
}

// true = desbloqueada -> rele en HIGH (comportamiento original).
// Muchos modulos de rele chinos son ACTIVOS EN LOW. Si el rele no conmuta al
// cambiar de estado, intercambia HIGH y LOW en esta funcion.
void setRelayUnlocked(bool unlocked) {
  digitalWrite(RELEPIN, unlocked ? HIGH : LOW);
}

void playTone() {
  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, 1000);
  delay(200);
  noTone(BUZZER_PIN);
  delay(5);
  tone(BUZZER_PIN, 500);
  delay(200);
  noTone(BUZZER_PIN);
}

void lockAlarm() {
  alarmState.locked = true;
  setLed(true);
  setRelayUnlocked(false);
  playTone();
  Serial.println("[ALARMA] Bloqueada");
}

void unlockAlarm() {
  alarmState.locked = false;
  setLed(false);
  setRelayUnlocked(true);
  playTone();
  Serial.println("[ALARMA] Desbloqueada");
}

// ---------------------------------------------------------------------------
// Callbacks BLE
// ---------------------------------------------------------------------------
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    clientConnected = true;

    // Al reconectar, forzamos que se envie el estado actual en cuanto el
    // telefono habilite las notificaciones.
    alarmState.lastNotified = !alarmState.locked;
    notificationsEnabled = false;

    Serial.println("[BLE] Cliente conectado");
  }

  void onDisconnect(BLEServer *server) override {
    clientConnected = false;
    notificationsEnabled = false;
    restartAdvertising = true;

    // Bloqueamos al perder la conexion, pero sin bloquear el stack BLE.
    pendingAction = ACTION_LOCK;

    Serial.println("[BLE] Cliente desconectado");
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    size_t len = pCharacteristic->getLength();
    const uint8_t *data = pCharacteristic->getData();

    if (data == nullptr || len == 0) {
      Serial.println("[BLE] Escritura vacia");
      return;
    }

    char plaintext[AES_MAX_PACKET + 1];
    if (!aesDecryptPacket(data, len, plaintext, sizeof(plaintext))) {
      Serial.printf("[BLE] Paquete invalido (%u bytes): ", (unsigned)len);
      for (size_t i = 0; i < len; i++) {
        Serial.printf("%02X ", data[i]);
      }
      Serial.println();
      Serial.println("[BLE] Revisa que la clave AES de la app sea la misma que keys.h");
      return;
    }

    Serial.printf("[BLE] Comando: '%s'\n", plaintext);

    if (strcmp(plaintext, "1") == 0) {
      pendingAction = ACTION_TOGGLE;
    } else {
      Serial.println("[BLE] Comando desconocido");
    }
  }
};

#if defined(CONFIG_BLUEDROID_ENABLED)
// En el core clasico (Bluedroid) el CCCD se crea a mano con BLE2902.
class StatusCccdCallbacks : public BLEDescriptorCallbacks {
  void onWrite(BLEDescriptor *pDescriptor) override {
    uint8_t *value = pDescriptor->getValue();
    bool enabled = (value != nullptr) && ((value[0] & 0x01) != 0);

    notificationsEnabled = enabled;

    if (enabled) {
      // Fuerza el envio del estado actual en el proximo loop.
      alarmState.lastNotified = !alarmState.locked;
      Serial.println("[BLE] Notificaciones activadas");
    } else {
      Serial.println("[BLE] Notificaciones desactivadas");
    }
  }
};
#endif

#if defined(CONFIG_NIMBLE_ENABLED)
// En ESP32-C3 el core usa NimBLE: el CCCD es automatico y la suscripcion
// se detecta con onSubscribe().
class StatusCallbacks : public BLECharacteristicCallbacks {
  void onSubscribe(BLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc, uint16_t subValue) override {
    bool enabled = (subValue & 0x01) != 0;

    notificationsEnabled = enabled;

    if (enabled) {
      alarmState.lastNotified = !alarmState.locked;
      Serial.println("[BLE] Notificaciones activadas");
    } else {
      Serial.println("[BLE] Notificaciones desactivadas");
    }
  }
};
#endif

// ---------------------------------------------------------------------------
// Notificacion de estado
// ---------------------------------------------------------------------------
void notifyStatusIfNeeded() {
  if (!clientConnected || !notificationsEnabled) {
    return;
  }
  if (alarmState.locked == alarmState.lastNotified) {
    return;
  }

  char message[8];
  snprintf(message, sizeof(message), "%d,%d",
           alarmState.locked ? 1 : 0,
           alarmState.lastNotified ? 1 : 0);

  uint8_t encrypted[AES_MAX_PACKET];
  size_t encryptedLen = aesEncryptText(message, encrypted, sizeof(encrypted));
  if (encryptedLen == 0) {
    Serial.println("[BLE] Error cifrando el estado");
    return;
  }

  statusCharacteristic->setValue(encrypted, encryptedLen);
  statusCharacteristic->notify();
  alarmState.lastNotified = alarmState.locked;

  Serial.printf("[BLE] Estado notificado: %s\n", message);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(LEDPIN, OUTPUT);
  pinMode(RELEPIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  setLed(true);
  setRelayUnlocked(false);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println();
  Serial.println("==========================================");
  Serial.printf(" Alarma Moto BLE - placa: %s\n", BOARD_NAME);
  Serial.printf(" LED=%d  RELE=%d  BUZZER=%d\n", LEDPIN, RELEPIN, BUZZER_PIN);
  Serial.println("==========================================");

  BLEDevice::init("AlarmaMoto");
  Serial.printf("[BLE] MAC: %s\n", BLEDevice::getAddress().toString().c_str());

#if REQUIRE_ENCRYPTED_WRITE
  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  pSecurity->setCapability(ESP_IO_CAP_NONE);
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
#endif

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  statusCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_STATUS,
    BLECharacteristic::PROPERTY_NOTIFY
  );

#if defined(CONFIG_BLUEDROID_ENABLED)
  statusCccd = new BLE2902();
  statusCccd->setCallbacks(new StatusCccdCallbacks());
  statusCharacteristic->addDescriptor(statusCccd);
#elif defined(CONFIG_NIMBLE_ENABLED)
  statusCharacteristic->setCallbacks(new StatusCallbacks());
#endif

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new RxCallbacks());

#if REQUIRE_ENCRYPTED_WRITE
  pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
#else
  pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
#endif

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);   // antes se llamaba dos veces a setMinPreferred

  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising iniciado como \"AlarmaMoto\"");
  Serial.println("[ALARMA] Estado inicial: bloqueada");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
  // 1) Acciones pedidas desde callbacks BLE (nunca se bloquea el stack BLE).
  PendingAction action = pendingAction;
  if (action != ACTION_NONE) {
    pendingAction = ACTION_NONE;

    if (action == ACTION_TOGGLE) {
      action = alarmState.locked ? ACTION_UNLOCK : ACTION_LOCK;
    }

    if (action == ACTION_LOCK) {
      lockAlarm();
    } else if (action == ACTION_UNLOCK) {
      unlockAlarm();
    }
  }

  // 2) Reiniciar advertising tras una desconexion.
  if (restartAdvertising) {
    restartAdvertising = false;
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising reiniciado");
  }

  // 3) LED: fijo conectado, parpadeo no bloqueante desconectado.
  if (clientConnected) {
    setLed(alarmState.locked);
  } else {
    unsigned long now = millis();
    if (now - lastBlinkMs >= 500) {
      lastBlinkMs = now;
      blinkState = !blinkState;
      setLed(blinkState);
    }
  }

  // 4) Avisar cambios de estado al telefono.
  notifyStatusIfNeeded();

  delay(10);
}
