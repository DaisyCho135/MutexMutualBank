#ifndef SECURITY_H
#define SECURITY_H
#include <stddef.h>

void xor_cipher(void *data, size_t len);
void aes_encrypt(void *in, void *out, size_t len);
void aes_decrypt(void *in, void *out, size_t len);

#endif