#include "aes.h"
#include "keys.h"

#include <mbedtls/aes.h>
#include <string.h>

#define AES_BLOCK_SIZE 16

static const uint8_t AES_IV[AES_BLOCK_SIZE] = {
  0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

bool aesDecryptPacket(const uint8_t *input, size_t inputLen, char *output, size_t outputSize) {
  if (input == nullptr || output == nullptr || outputSize == 0) {
    return false;
  }
  if (inputLen == 0 || inputLen % AES_BLOCK_SIZE != 0 || inputLen > AES_MAX_PACKET) {
    return false;
  }

  uint8_t decrypted[AES_MAX_PACKET];
  unsigned char iv[AES_BLOCK_SIZE];
  memcpy(iv, AES_IV, sizeof(iv));

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  int ret = mbedtls_aes_setkey_dec(&aes, (const unsigned char *)AES_KEY, AES_KEY_BITS);
  if (ret == 0) {
    ret = mbedtls_aes_crypt_cbc(
      &aes,
      MBEDTLS_AES_DECRYPT,
      inputLen,
      iv,
      input,
      decrypted
    );
  }
  mbedtls_aes_free(&aes);

  if (ret != 0) {
    return false;
  }

  // Validar PKCS7. Si la clave no coincide el padding casi nunca sera valido.
  uint8_t padValue = decrypted[inputLen - 1];
  if (padValue == 0 || padValue > AES_BLOCK_SIZE || padValue > inputLen) {
    return false;
  }
  for (size_t i = inputLen - padValue; i < inputLen; i++) {
    if (decrypted[i] != padValue) {
      return false;
    }
  }

  size_t plainLen = inputLen - padValue;
  if (plainLen + 1 > outputSize) {
    return false;
  }

  memcpy(output, decrypted, plainLen);
  output[plainLen] = '\0';
  return true;
}

size_t aesEncryptText(const char *input, uint8_t *output, size_t outputSize) {
  if (input == nullptr || output == nullptr) {
    return 0;
  }

  size_t inputLen = strlen(input);
  size_t paddedLen = ((inputLen / AES_BLOCK_SIZE) + 1) * AES_BLOCK_SIZE;
  if (paddedLen > outputSize || paddedLen > AES_MAX_PACKET) {
    return 0;
  }

  uint8_t padded[AES_MAX_PACKET];
  memcpy(padded, input, inputLen);

  uint8_t padValue = (uint8_t)(paddedLen - inputLen);
  memset(padded + inputLen, padValue, padValue);

  unsigned char iv[AES_BLOCK_SIZE];
  memcpy(iv, AES_IV, sizeof(iv));

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  int ret = mbedtls_aes_setkey_enc(&aes, (const unsigned char *)AES_KEY, AES_KEY_BITS);
  if (ret == 0) {
    ret = mbedtls_aes_crypt_cbc(
      &aes,
      MBEDTLS_AES_ENCRYPT,
      paddedLen,
      iv,
      padded,
      output
    );
  }
  mbedtls_aes_free(&aes);

  return ret == 0 ? paddedLen : 0;
}
