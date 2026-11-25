#pragma once

// ---------------------------------------------------------------------------
// Perfiles de pines por placa.
//
// IMPORTANTE (ESP32 clasico / DevKit / WROOM):
//   GPIO 6..11 estan conectados a la memoria flash SPI. NO se pueden usar
//   como entradas/salidas. Por eso el relay NO puede ir en GPIO 10 ni el LED
//   en GPIO 8 en una placa ESP32 clasica.
//   GPIO 0 es el boton BOOT/strapping: evitarlo para el buzzer.
//
// En ESP32-C3 los pines 8 y 10 si son normales (aunque 8 y 9 son strapping).
// Se mantiene el cableado original de la C3 para no romper esa placa.
// ---------------------------------------------------------------------------

#if defined(CONFIG_IDF_TARGET_ESP32C3)

  #define BOARD_NAME  "ESP32-C3"
  #define LEDPIN       8    // LED (en muchas DevKit C3 el 8 es el RGB integrado)
  #define RELEPIN      10
  #define BUZZER_PIN   0    // strapping en C3; si puedes, muevelo a GPIO 3

#elif defined(CONFIG_IDF_TARGET_ESP32)

  #define BOARD_NAME  "ESP32"
  #define LEDPIN       33    // LED integrado del DevKit
  #define RELEPIN      2
  #define BUZZER_PIN   15   // buzzer pasivo o activo (tone() funciona con ambos)
  
  // ---------------------------------------------------------------------------
  //
  //   POWER_PIN    -> "Encender"  (comando "2", tipo toggle)
  //   STARTER_PIN  -> "Arrancar"  (comando "3", momentaneo / mantener)
  //   BUTTON1_PIN  -> "Boton 1"   (comando "4", momentaneo / mantener)
  //   BUTTON2_PIN  -> "Boton 2"   (comando "5", momentaneo / mantener)
  //
  // En ESP32 clasico GPIO 12 y 15 son pines de strapping. Se pueden usar como
  // salida despues del arranque, pero evita que un modulo externo los fuerce
  // durante el boot (GPIO 15 debe estar en LOW al encender).
  // ---------------------------------------------------------------------------
  #define POWER_PIN     14
  #define STARTER_PIN   12
  #define BUTTON1_PIN   4
  #define BUTTON2_PIN   4

#else
  #error "Este sketch solo soporta ESP32 clasico y ESP32-C3"
#endif

// Tiempo maximo que una salida momentanea puede quedar activa sin recibir
// un nuevo comando de "mantener presionado". Es una red de seguridad por si
// el telefono se desconecta, se cierra la app o se pierde el comando de
// liberacion. La app reenvia el comando cada 500 ms mientras el boton sigue
// presionado.
#define HOLD_TIMEOUT_MS 1500

// ---------------------------------------------------------------------------
// Seguridad BLE
//
// 0 = no exigir enlace cifrado (RECOMENDADO para empezar). El comando "1"
//     ya viaja cifrado con AES-CBC a nivel de aplicacion.
// 1 = exigir que el cliente este emparejado/cifrado para escribir en RX.
//     Si lo activas, hay que emparejar el telefono y, tras regrabar el
//     firmware o borrar la NVS, borrar el emparejamiento antiguo en Android.
//     IMPORTANTE: Esta configuracion suele dar problemas
// ---------------------------------------------------------------------------
#ifndef REQUIRE_ENCRYPTED_WRITE
#define REQUIRE_ENCRYPTED_WRITE 0
#endif
