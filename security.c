#include "security.h"
#include <openssl/evp.h>

/*
Hardcoded AES Key and Initialization Vector (IV).
Note: In a production system, these should be securely managed
(e.g., using a Key Management Service or environment variables)
rather than hardcoded in the source.
*/
static const unsigned char aes_key[16] = "0123456789abcdef";
static const unsigned char aes_iv[16]  = "abcdef9876543210";

/*
Simple XOR Cipher.
Used for lightweight obfuscation of non-sensitive transaction data.
It is fast but does not provide strong cryptographic security.
 */
void xor_cipher(void *data, size_t len) {
    unsigned char *p = (unsigned char *)data;
    for (size_t i = 0; i < len; i++) p[i] ^= 0xAA;
}

/*
AES Decryption using OpenSSL EVP API.
Algorithm: AES-128-CBC
*/
void aes_encrypt(void *in, void *out, size_t len) {
    //Initialize encryption context with AES-128-CBC
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int outlen1, outlen2;
    EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, aes_key, aes_iv);
    /*
    Disable standard padding (PKCS#7).
    IMPORTANT: The input 'len' must be a multiple of the block size (16 bytes).
    This works here because our structs (LoginRequest) are manually padded/aligned.
    */
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    //Encrypt the data
    EVP_EncryptUpdate(ctx, out, &outlen1, in, len);
    //Finalize encryption (handles any remaining bytes, though none expected here due to no padding)
    EVP_EncryptFinal_ex(ctx, (unsigned char*)out + outlen1, &outlen2);
    EVP_CIPHER_CTX_free(ctx);
}

void aes_decrypt(void *in, void *out, size_t len) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int outlen1, outlen2;
    EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, aes_key, aes_iv);
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    EVP_DecryptUpdate(ctx, out, &outlen1, in, len);
    EVP_DecryptFinal_ex(ctx, (unsigned char*)out + outlen1, &outlen2);
    EVP_CIPHER_CTX_free(ctx);
}