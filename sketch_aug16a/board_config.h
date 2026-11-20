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
  #define LEDPIN       2    // LED integrado del DevKit
  #define RELEPIN      26
  #define BUZZER_PIN   27   // buzzer pasivo o activo (tone() funciona con ambos)

#else
  #error "Este sketch solo soporta ESP32 clasico y ESP32-C3"
#endif

// ---------------------------------------------------------------------------
// Seguridad BLE
//
// 0 = no exigir enlace cifrado (RECOMENDADO para empezar). El comando "1"
//     ya viaja cifrado con AES-CBC a nivel de aplicacion.
// 1 = exigir que el cliente este emparejado/cifrado para escribir en RX.
//     Si lo activas, hay que emparejar el telefono y, tras regrabar el
//     firmware o borrar la NVS, borrar el emparejamiento antiguo en Android.
// ---------------------------------------------------------------------------
#ifndef REQUIRE_ENCRYPTED_WRITE
#define REQUIRE_ENCRYPTED_WRITE 0
#endif
