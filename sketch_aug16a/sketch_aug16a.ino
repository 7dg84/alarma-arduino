#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

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
  bool powerOn = false;       // GPIO POWER_PIN (encender)
  bool starterActive = false; // boton "Arrancar" mantenido
  bool button1Active = false; // boton "Boton 1" mantenido
  bool button2Active = false; // boton "Boton 2" mantenido
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

// Comandos que llegan desde la app. Se encolan en el callback BLE y se
// procesan en el loop para no bloquear el stack de Bluetooth.
enum Command : uint8_t {
  CMD_NONE = 0,
  CMD_TOGGLE_LOCK,     // "1"
  CMD_TOGGLE_POWER,    // "2"
  CMD_STARTER_DOWN,    // "3"
  CMD_STARTER_UP,      // "3u"
  CMD_BUTTON1_DOWN,    // "4"
  CMD_BUTTON1_UP,      // "4u"
  CMD_BUTTON2_DOWN,    // "5"
  CMD_BUTTON2_UP       // "5u"
};

QueueHandle_t commandQueue = nullptr;

unsigned long lastStarterRefreshMs = 0;
unsigned long lastButton1RefreshMs = 0;
unsigned long lastButton2RefreshMs = 0;

// Ultimo estado enviado al telefono. Se envia de nuevo cuando cambia.
char lastStatusSent[32] = "";

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

// Aplica el estado de las salidas nuevas. "Arrancar" y "Boton 1" pueden
// compartir GPIO 15: en ese caso la linea se activa con un OR logico.
void applyAuxOutputs() {
  digitalWrite(POWER_PIN, (alarmState.powerOn && !alarmState.locked) ? HIGH : LOW);

#if BUTTON2_PIN == BUTTON1_PIN
  bool starterLine = alarmState.button2Active || alarmState.button1Active;
  digitalWrite(BUTTON1_PIN, starterLine ? HIGH : LOW);
#else
  digitalWrite(BUTTON1_PIN, alarmState.button1Active ? HIGH : LOW);
  digitalWrite(BUTTON2_PIN, alarmState.button2Active ? HIGH : LOW);
#endif
digitalWrite(STARTER_PIN, (alarmState.starterActive && alarmState.powerOn && !alarmState.locked) ? LOW : HIGH);
}

// Apaga todas las salidas momentaneas (arranque y botones 1/2).
void releaseMomentaryOutputs() {
  alarmState.starterActive = false;
  alarmState.button1Active = false;
  alarmState.button2Active = false;
  applyAuxOutputs();
}

// Encola un comando para que lo procese el loop (nunca bloquea el stack BLE).
void enqueueCommand(Command command) {
  if (commandQueue == nullptr) {
    return;
  }
  if (xQueueSend(commandQueue, &command, 0) != pdTRUE) {
    Serial.println("[BLE] Cola de comandos llena, comando descartado");
  }
}

// Descarta comandos pendientes. Se usa al conectar/desconectar para que un
// comando viejo no reactive una salida despues de la liberacion de seguridad.
void clearCommandQueue() {
  if (commandQueue != nullptr) {
    xQueueReset(commandQueue);
  }
}

// void playTone() {
//   noTone(BUZZER_PIN);
//   tone(BUZZER_PIN, 1000);
//   delay(200);
//   noTone(BUZZER_PIN);
//   delay(5);
//   tone(BUZZER_PIN, 500);
//   delay(200);
//   noTone(BUZZER_PIN);
// }
void playTone() {
  digitalWrite(BUZZER_PIN, 0);
  digitalWrite(BUZZER_PIN, 1);
  delay(200);
  digitalWrite(BUZZER_PIN, 0);
  delay(5);
  digitalWrite(BUZZER_PIN, 1);
  delay(200);
  digitalWrite(BUZZER_PIN, 0);
}

void lockAlarmState() {
  alarmState.locked = true;
  setLed(true);
  setRelayUnlocked(false);
}

void lockAlarm() {
  lockAlarmState();
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

void togglePower() {
  alarmState.powerOn = !alarmState.powerOn;
  applyAuxOutputs();
  Serial.printf("[SALIDA] Encendido: %s\n", alarmState.powerOn ? "ON" : "OFF");
}

void powerOff() {
  alarmState.powerOn = false;
  applyAuxOutputs();
  Serial.printf("[SALIDA] Encendido: %s\n", alarmState.powerOn ? "ON" : "OFF");
}

void handleCommand(Command command) {
  unsigned long now = millis();

  switch (command) {
    case CMD_TOGGLE_LOCK:
      if (alarmState.locked) {
        unlockAlarm();
      } else {
        lockAlarm();
      }
      applyAuxOutputs();
      break;

    case CMD_TOGGLE_POWER:
      togglePower();
      break;

    case CMD_STARTER_DOWN:
      alarmState.starterActive = true;
      lastStarterRefreshMs = now;
      applyAuxOutputs();
      break;

    case CMD_STARTER_UP:
      alarmState.starterActive = false;
      applyAuxOutputs();
      break;

    case CMD_BUTTON1_DOWN:
      alarmState.button1Active = true;
      lastButton1RefreshMs = now;
      applyAuxOutputs();
      break;

    case CMD_BUTTON1_UP:
      alarmState.button1Active = false;
      applyAuxOutputs();
      break;

    case CMD_BUTTON2_DOWN:
      alarmState.button2Active = true;
      lastButton2RefreshMs = now;
      applyAuxOutputs();
      break;

    case CMD_BUTTON2_UP:
      alarmState.button2Active = false;
      applyAuxOutputs();
      break;

    default:
      break;
  }
}

// Si la app deja de reenviar el comando de "mantener", apagamos la salida.
// Esto cubre cierres de app, perdida de paquetes o desconexiones.
void checkMomentaryTimeouts() {
  unsigned long now = millis();
  bool changed = false;

  if (alarmState.starterActive && (now - lastStarterRefreshMs > HOLD_TIMEOUT_MS)) {
    alarmState.starterActive = false;
    changed = true;
    Serial.println("[SALIDA] Arranque liberado por timeout");
  }

  if (alarmState.button1Active && (now - lastButton1RefreshMs > HOLD_TIMEOUT_MS)) {
    alarmState.button1Active = false;
    changed = true;
    Serial.println("[SALIDA] Boton 1 liberado por timeout");
  }

  if (alarmState.button2Active && (now - lastButton2RefreshMs > HOLD_TIMEOUT_MS)) {
    alarmState.button2Active = false;
    changed = true;
    Serial.println("[SALIDA] Boton 2 liberado por timeout");
  }

  if (changed) {
    applyAuxOutputs();
  }
}

// Traduce el texto descifrado a un comando interno.
//   "1"  toggle bloqueo
//   "2"  toggle encendido (GPIO 14)
//   "3"  arranque abajo (GPIO 15)   / "3u" arranque arriba
//   "4"  boton 1 abajo (GPIO 15)    / "4u" boton 1 arriba
//   "5"  boton 2 abajo (GPIO 12)    / "5u" boton 2 arriba
bool parseCommand(const char *text, Command *command) {
  if (strcmp(text, "1") == 0) {
    *command = CMD_TOGGLE_LOCK;
  } else if (strcmp(text, "2") == 0) {
    *command = CMD_TOGGLE_POWER;
  } else if (strcmp(text, "3") == 0) {
    *command = CMD_STARTER_DOWN;
  } else if (strcmp(text, "3u") == 0 || strcmp(text, "3U") == 0) {
    *command = CMD_STARTER_UP;
  } else if (strcmp(text, "4") == 0) {
    *command = CMD_BUTTON1_DOWN;
  } else if (strcmp(text, "4u") == 0 || strcmp(text, "4U") == 0) {
    *command = CMD_BUTTON1_UP;
  } else if (strcmp(text, "5") == 0) {
    *command = CMD_BUTTON2_DOWN;
  } else if (strcmp(text, "5u") == 0 || strcmp(text, "5U") == 0) {
    *command = CMD_BUTTON2_UP;
  } else {
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Callbacks BLE
// ---------------------------------------------------------------------------
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    clientConnected = true;

    // Al reconectar, forzamos que se envie el estado actual en cuanto el
    // telefono habilite las notificaciones.
    lastStatusSent[0] = '\0';
    notificationsEnabled = false;

    // Comandos de la sesion anterior no deben ejecutarse en esta.
    clearCommandQueue();

    Serial.println("[BLE] Cliente conectado");
  }

  void onDisconnect(BLEServer *server) override {
    clientConnected = false;
    notificationsEnabled = false;
    restartAdvertising = true;
    lastStatusSent[0] = '\0';

    // Seguridad: si se cae la conexion con un boton momentaneo presionado,
    // las salidas deben apagarse de inmediato. Se bloquea sin tone()/delay()
    // para no bloquear el callback del stack BLE.
    clearCommandQueue();
    releaseMomentaryOutputs();
    powerOff();
    lockAlarmState();
    
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

    Command command = CMD_NONE;
    if (parseCommand(plaintext, &command)) {
      enqueueCommand(command);
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
      lastStatusSent[0] = '\0';
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
      lastStatusSent[0] = '\0';
      Serial.println("[BLE] Notificaciones activadas");
    } else {
      Serial.println("[BLE] Notificaciones desactivadas");
    }
  }
};
#endif

// ---------------------------------------------------------------------------
// Notificacion de estado
//
// Formato enviado (cifrado con AES-CBC):
//   conectado,bloqueado,encendido,arranque,boton1,boton2
// Ejemplo: "1,1,0,0,0,0"
// ---------------------------------------------------------------------------
void buildStatusMessage(char *message, size_t size) {
  snprintf(message, size,
           "%d,%d,%d,%d,%d,%d",
           clientConnected ? 1 : 0,
           alarmState.locked ? 1 : 0,
           alarmState.powerOn ? 1 : 0,
           alarmState.starterActive ? 1 : 0,
           alarmState.button1Active ? 1 : 0,
           alarmState.button2Active ? 1 : 0);
}

void notifyStatusIfNeeded() {
  if (!clientConnected || !notificationsEnabled) {
    return;
  }

  char message[32];
  buildStatusMessage(message, sizeof(message));

  if (strcmp(message, lastStatusSent) == 0) {
    return;
  }

  uint8_t encrypted[AES_MAX_PACKET];
  size_t encryptedLen = aesEncryptText(message, encrypted, sizeof(encrypted));
  if (encryptedLen == 0) {
    Serial.println("[BLE] Error cifrando el estado");
    return;
  }

  statusCharacteristic->setValue(encrypted, encryptedLen);
  statusCharacteristic->notify();

  strncpy(lastStatusSent, message, sizeof(lastStatusSent) - 1);
  lastStatusSent[sizeof(lastStatusSent) - 1] = '\0';

  Serial.printf("[BLE] Estado notificado: %s\n", message);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  commandQueue = xQueueCreate(16, sizeof(Command));
  if (commandQueue == nullptr) {
    Serial.println("[BLE] ERROR: no se pudo crear la cola de comandos");
  }

  pinMode(LEDPIN, OUTPUT);
  pinMode(RELEPIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(POWER_PIN, OUTPUT);
  pinMode(STARTER_PIN, OUTPUT);
#if BUTTON1_PIN != STARTER_PIN
  pinMode(BUTTON1_PIN, OUTPUT);
#endif
  pinMode(BUTTON2_PIN, OUTPUT);

  setLed(true);
  setRelayUnlocked(false);
  digitalWrite(BUZZER_PIN, LOW);
  applyAuxOutputs();

  Serial.println();
  Serial.println("==========================================");
  Serial.printf(" Alarma Moto BLE - placa: %s\n", BOARD_NAME);
  Serial.printf(" LED=%d  RELE=%d  BUZZER=%d\n", LEDPIN, RELEPIN, BUZZER_PIN);
  Serial.printf(" POWER=%d  STARTER=%d  BUTTON1=%d  BUTTON2=%d\n",
                POWER_PIN, STARTER_PIN, BUTTON1_PIN, BUTTON2_PIN);
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
  // 1) Procesar todos los comandos encolados por el callback BLE.
  if (commandQueue != nullptr) {
    Command command;
    while (xQueueReceive(commandQueue, &command, 0) == pdTRUE) {
      handleCommand(command);
    }
  }

  // 2) Red de seguridad: liberar botones momentaneos sin refresco.
  checkMomentaryTimeouts();

  // 3) Reiniciar advertising tras una desconexion.
  if (restartAdvertising) {
    restartAdvertising = false;
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising reiniciado");
  }

  // 4) LED: fijo conectado, parpadeo no bloqueante desconectado.
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

  // 5) Avisar cambios de estado al telefono.
  notifyStatusIfNeeded();

  delay(10);
}
