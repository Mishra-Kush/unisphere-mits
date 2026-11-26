#include <stdio.h>
#include <string.h>
#include "sha256.h"
#include "password.h"
static int fails = 0;
static void hex(const uint8_t *b, size_t n, char *o) { for (size_t i = 0; i < n; i++) sprintf(o + 2*i, "%02x", b[i]); }
static void check(const char *name, int ok) { printf("%s %s\n", ok ? "PASS" : "FAIL", name); fails += !ok; }
int main(void) {
    uint8_t d[32], dk[32]; char h[65]; sha256_ctx c;
    sha256_init(&c); sha256_update(&c, "abc", 3); sha256_final(&c, d); hex(d, 32, h);
    check("SHA-256 'abc'", !strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    sha256_init(&c); sha256_final(&c, d); hex(d, 32, h);
    check("SHA-256 empty", !strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    hmac_sha256((const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28, d); hex(d, 32, h);
    check("HMAC-SHA256 RFC 4231 case 2", !strcmp(h, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    pbkdf2_sha256((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 1, dk, 32); hex(dk, 32, h);
    check("PBKDF2 c=1", !strcmp(h, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
    pbkdf2_sha256((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 4096, dk, 32); hex(dk, 32, h);
    check("PBKDF2 c=4096", !strcmp(h, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));
    char a[PASSWORD_HASH_MAX], b[PASSWORD_HASH_MAX];
    check("hash created", password_hash("12120", a, sizeof a) == 0 && password_hash("12120", b, sizeof b) == 0);
    check("correct password verifies", password_verify("12120", a));
    check("wrong password rejected", !password_verify("12121", a) && !password_verify("", a));
    check("same password gives different salted hashes", strcmp(a, b) != 0);
    check("hash does not contain plaintext", strstr(a, "12120") == NULL);
    check("malformed stored hash rejected", !password_verify("x", "garbage") && !password_verify("x", "pbkdf2_sha256$1$zz$zz"));
    printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails != 0;
}
