#pragma once

#include <stddef.h>
#include <stdint.h>

// Descifra un paquete AES-CBC/PKCS7 recibido por BLE.
// Devuelve true solo si el paquete es valido y el padding es correcto.
bool aesDecryptPacket(const uint8_t *input, size_t inputLen, char *output, size_t outputSize);

// Cifra un texto terminado en '\0' con AES-CBC/PKCS7.
// Devuelve la cantidad de bytes cifrados, o 0 si hubo error.
size_t aesEncryptText(const char *input, uint8_t *output, size_t outputSize);
