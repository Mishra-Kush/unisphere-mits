#include "password.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

#define SALT_LEN 16
#define HASH_LEN 32

int random_bytes(uint8_t *buf, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(NULL, buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    size_t r;
    if (!f) return -1;
    r = fread(buf, 1, n, f);
    fclose(f);
    return r == n ? 0 : -1;
#endif
}

static void to_hex(const uint8_t *in, size_t n, char *out) {
    static const char *d = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) { out[2*i] = d[in[i] >> 4]; out[2*i+1] = d[in[i] & 15]; }
    out[2*n] = '\0';
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int from_hex(const char *s, size_t n, uint8_t *out) {
    size_t i;
    for (i = 0; i < n; i++) {
        int hi = hexval(s[2*i]), lo = hexval(s[2*i+1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 0;
}

int password_hash(const char *password, char *out, size_t outsz) {
    uint8_t salt[SALT_LEN], dk[HASH_LEN];
    char sh[SALT_LEN*2+1], hh[HASH_LEN*2+1];
    if (outsz < PASSWORD_HASH_MAX || !password) return -1;
    if (random_bytes(salt, SALT_LEN) != 0) return -1;
    pbkdf2_sha256((const uint8_t *)password, strlen(password), salt, SALT_LEN, PASSWORD_ITERATIONS, dk, HASH_LEN);
    to_hex(salt, SALT_LEN, sh);
    to_hex(dk, HASH_LEN, hh);
    snprintf(out, outsz, "pbkdf2_sha256$%d$%s$%s", PASSWORD_ITERATIONS, sh, hh);
    return 0;
}

int password_verify(const char *password, const char *stored) {
    const char *p, *salt, *sep, *h;
    char *end;
    unsigned long it;
    uint8_t s[SALT_LEN], expect[HASH_LEN], got[HASH_LEN];
    unsigned char diff = 0;
    int i;
    if (!password || !stored || strncmp(stored, "pbkdf2_sha256$", 14) != 0) return 0;
    p = stored + 14;
    it = strtoul(p, &end, 10);
    if (*end != '$' || it < 1 || it > 10000000UL) return 0;
    salt = end + 1;
    sep = strchr(salt, '$');
    if (!sep || (size_t)(sep - salt) != SALT_LEN * 2) return 0;
    h = sep + 1;
    if (strlen(h) != HASH_LEN * 2) return 0;
    if (from_hex(salt, SALT_LEN, s) || from_hex(h, HASH_LEN, expect)) return 0;
    pbkdf2_sha256((const uint8_t *)password, strlen(password), s, SALT_LEN, (uint32_t)it, got, HASH_LEN);
    for (i = 0; i < HASH_LEN; i++) diff |= (unsigned char)(got[i] ^ expect[i]);  /* constant-time compare */
    return diff == 0;
}
