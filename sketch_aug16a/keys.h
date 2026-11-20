#pragma once

#include <stddef.h>

// Clave compartida con la app Android (BleConstants.DEFAULT_KEY).
// AES acepta claves de 16, 24 o 32 caracteres (AES-128/192/256).
static const char AES_KEY[] = "abcdefghijklmnop";

static_assert(
  (sizeof(AES_KEY) - 1) == 16 || (sizeof(AES_KEY) - 1) == 24 || (sizeof(AES_KEY) - 1) == 32,
  "AES_KEY debe tener exactamente 16, 24 o 32 caracteres"
);

#define AES_KEY_BITS ((sizeof(AES_KEY) - 1) * 8)

// Tamano maximo de un paquete AES que aceptamos por BLE.
#define AES_MAX_PACKET 64
