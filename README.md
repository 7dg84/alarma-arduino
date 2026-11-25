# Firmware Arduino / ESP32 - Alarma Moto BLE

Firmware para **ESP32** (clásico) y **ESP32-C3** que implementa un servidor BLE GATT (`AlarmaMoto`) con comunicación cifrada mediante **AES-CBC (PKCS7)** para controlar el bloqueo de la moto, encendido, arranque y salidas auxiliares desde la app Android.

## Estructura

```
arduino/
├── README.md
└── sketch_aug16a/
    ├── sketch_aug16a.ino   # Servidor BLE, cola de comandos FreeRTOS, lógica de alarma y salidas
    ├── board_config.h      # Perfiles de pines por placa, timeout de seguridad y opciones BLE
    ├── aes.h               # Declaraciones de cifrado/descifrado AES-CBC
    ├── aes.ino             # Implementación con mbedtls (AES-CBC + PKCS7)
    └── keys.h              # Clave compartida AES_KEY y tamaño máximo de paquete
```

## Mapeo de Pines (`board_config.h`)

### ESP32 clásico (`CONFIG_IDF_TARGET_ESP32`)

| Macro | GPIO | Función | Lógica / Notas |
|---|---|---|---|
| `LEDPIN` | `33` | LED de estado | `HIGH` = encendido. Fijo si conectado (indica bloqueo), parpadeo cada 500 ms si desconectado. |
| `RELEPIN` | `2` | Relé principal (bloqueo) | `HIGH` = desbloqueada, `LOW` = bloqueada (ajustable en `setRelayUnlocked()` si el relé es activo en `LOW`). |
| `BUZZER_PIN` | `15` | Buzzer (activo o pasivo) | Doble pulso de 200 ms al bloquear/desbloquear. Pin de strapping: evitar forzarlo en `HIGH` al arrancar. |
| `POWER_PIN` | `14` | Encendido (`Encender`) | Toggle (`HIGH` solo cuando `powerOn && !locked`). |
| `STARTER_PIN` | `12` | Arranque (`Arrancar`) | Momentáneo, **activo en `LOW`** (`LOW` solo cuando `starterActive && powerOn && !locked`, `HIGH` en reposo). Pin de strapping. |
| `BUTTON1_PIN` | `4` | Botón 1 | Momentáneo, activo en `HIGH`. |
| `BUTTON2_PIN` | `4` | Botón 2 | Momentáneo, activo en `HIGH`. Comparte GPIO `4` con `BUTTON1_PIN` mediante OR lógico (`button1Active \|\| button2Active`). |

> [!WARNING]
> En ESP32 clásico, los pines **GPIO 6–11** están reservados para la memoria flash SPI y no deben usarse. Los pines **GPIO 12 y 15** son pines de strapping; asegúrate de que ningún módulo externo fuerce un nivel incompatible durante el arranque.

### ESP32-C3 (`CONFIG_IDF_TARGET_ESP32C3`)

| Macro | GPIO | Función | Notas |
|---|---|---|---|
| `LEDPIN` | `8` | LED de estado | En muchas DevKit C3 corresponde al LED integrado. |
| `RELEPIN` | `10` | Relé principal | — |
| `BUZZER_PIN` | `0` | Buzzer | Pin de strapping en C3 (se recomienda mover a GPIO `3` si es posible). |

> [!NOTE]
> En `board_config.h`, `POWER_PIN`, `STARTER_PIN`, `BUTTON1_PIN` y `BUTTON2_PIN` están definidos actualmente en el perfil `CONFIG_IDF_TARGET_ESP32`. Si compilas para ESP32-C3 con las salidas auxiliares, define esos pines en el bloque `CONFIG_IDF_TARGET_ESP32C3`.

## Protocolo BLE

- **Nombre del dispositivo:** `AlarmaMoto`
- **Service UUID:** `e6067851-5971-4b21-a8cc-17738c56ea49`

### Características

| Característica | UUID | Propiedad | Descripción |
|---|---|---|---|
| `STATUS` | `db9ab4aa-da20-4de8-8a08-e14ab7e5148e` | `NOTIFY` | Notifica cambios de estado cifrados con AES-CBC en formato CSV: `conectado,bloqueado,encendido,arranque,boton1,boton2` (ej. `1,1,0,0,0,0`). |
| `RX` | `f0bf0a71-0dfa-4d1b-90ae-cfda669a37c0` | `WRITE` | Recibe comandos cifrados con AES-CBC desde la app. |

Soporta tanto el stack **Bluedroid** (`BLE2902` en ESP32 clásico) como **NimBLE** (`onSubscribe` en ESP32-C3).

### Comandos (`RX`)

| Comando | Acción | Tipo |
|---|---|---|
| `"1"` | Alterna bloqueo / desbloqueo (`RELEPIN` + buzzer) | Toggle |
| `"2"` | Alterna encendido (`POWER_PIN`, requiere estar desbloqueada para activar salida) | Toggle |
| `"3"` / `"3u"` | Arranque presionado / liberado (`STARTER_PIN`, requiere `powerOn && !locked`) | Momentáneo |
| `"4"` / `"4u"` | Botón 1 presionado / liberado (`BUTTON1_PIN`) | Momentáneo |
| `"5"` / `"5u"` | Botón 2 presionado / liberado (`BUTTON2_PIN`) | Momentáneo |

## Seguridad y Mecanismos de Protección

- **Cifrado a nivel de aplicación (AES-CBC + PKCS7):** Todos los comandos recibidos en `RX` y notificaciones enviadas en `STATUS` se cifran usando `mbedtls`. La clave se configura en `keys.h` (`AES_KEY`, por defecto `"abcdefghijklmnop"`, 16 caracteres / AES-128; también admite 24 o 32 caracteres para AES-192/256) y debe coincidir exactamente con la clave configurada en la app Android.
- **Watchdog de botones momentáneos (`HOLD_TIMEOUT_MS = 1500` ms):** Mientras un botón momentáneo (`Arrancar`, `Botón 1`, `Botón 2`) está presionado, la app reenvía el comando cada 500 ms. Si el firmware deja de recibirlo por más de 1.5 segundos, apaga la salida automáticamente.
- **Protección por desconexión:** Al desconectarse el cliente BLE (`onDisconnect`), el firmware limpia la cola de comandos, apaga las salidas momentáneas, apaga `POWER_PIN`, bloquea la alarma (`locked = true`) y reinicia el advertising BLE.
- **Cola FreeRTOS (`commandQueue`):** Los comandos BLE se encolan en el callback y se procesan en `loop()` (capacidad de 16 comandos) para no bloquear el stack Bluetooth.
- **Enlace BLE (`REQUIRE_ENCRYPTED_WRITE`):** Desactivado por defecto (`0`) en `board_config.h` para evitar problemas de emparejamiento/bonding en Android, ya que los datos ya viajan cifrados con AES-CBC.

## Compilación y Carga

1. Abre `arduino/sketch_aug16a/sketch_aug16a.ino` en **Arduino IDE** (o PlatformIO / Arduino CLI) con el core **ESP32** instalado.
2. Selecciona tu placa (**ESP32 Dev Module** o **ESP32C3 Dev Module**).
3. Ajusta `AES_KEY` en `keys.h` si deseas cambiar la clave compartida (16, 24 o 32 caracteres).
4. Compila y sube el sketch; abre el Monitor Serie a **115200 baudios** para ver la MAC BLE, los pines configurados y los logs de estado.
