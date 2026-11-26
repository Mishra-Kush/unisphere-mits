#ifndef PASSWORD_H
#define PASSWORD_H
#include <stddef.h>
#include <stdint.h>
#define PASSWORD_HASH_MAX 160
#define PASSWORD_ITERATIONS 100000
/* Stored format: pbkdf2_sha256$<iterations>$<salt hex>$<hash hex> */
int random_bytes(uint8_t *buf, size_t n);                      /* 0 = ok */
int password_hash(const char *password, char *out, size_t outsz); /* 0 = ok */
int password_verify(const char *password, const char *stored);    /* 1 = match */
#endif
