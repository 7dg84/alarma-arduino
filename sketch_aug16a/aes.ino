#include "mbedtls/aes.h"
#include "mbedtls/cipher.h"

#define BLOCK_SIZE 16


// Función para agregar PKCS7 padding
int addPKCS7Padding(const char* input, unsigned char* output, int inputLen) {
	int paddedLen = ((inputLen / BLOCK_SIZE) + 1) * BLOCK_SIZE;
	memcpy(output, input, inputLen);

	// Rellena los bytes restantes con el valor del padding
	uint8_t padValue = paddedLen - inputLen;
	for (int i = inputLen; i < paddedLen; i++) {
		output[i] = padValue;
	}
	return paddedLen;
}

void encrypt(const char* input, const char* key, uint8_t* output, int* outputLen) {
	unsigned char iv[] = { 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		                     0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };

	// Buffer con padding
	unsigned char paddedInput[256];
	int paddedLen = addPKCS7Padding(input, paddedInput, strlen(input));
	*outputLen = paddedLen;

	mbedtls_aes_context aes;
	mbedtls_aes_init(&aes);
	mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, strlen(key) * 8);
	mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv, paddedInput, output);
	mbedtls_aes_free(&aes);
}

void decrypt(unsigned char* input, char* key, unsigned char* output) {
	unsigned char iv[] = { 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		                     0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };

	unsigned char decrypted[INPUT_LENGTH];

	mbedtls_aes_context aes;
	mbedtls_aes_init(&aes);
	mbedtls_aes_setkey_dec(&aes, (const unsigned char*)key, strlen(key) * 8);
	mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, INPUT_LENGTH, iv, input, decrypted);
	mbedtls_aes_free(&aes);

	uint8_t padValue = decrypted[INPUT_LENGTH - 1];  // El último byte indica cuántos bytes de padding hay
	int realLength = INPUT_LENGTH - padValue;

	memcpy(output, decrypted, realLength);
	output[realLength] = '\0';  // Terminar el string
}
