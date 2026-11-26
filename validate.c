#include "validate.h"
#include <ctype.h>
#include <string.h>

int valid_email_format(char *e) {
    size_t i, n = strlen(e), local;
    char *at = strchr(e, '@'), *d;
    if (n < 5 || n > 254 || !at || at == e || strchr(at + 1, '@')) return 0;
    local = (size_t)(at - e);
    if (local > 64) return 0;
    for (i = 0; i < n; i++) e[i] = (char)tolower((unsigned char)e[i]);
    for (i = 0; i < local; i++)
        if (!isalnum((unsigned char)e[i]) && !strchr("._%+-", e[i])) return 0;
    d = at + 1;
    if (!strchr(d, '.') || *d == '.' || *d == '-' || d[strlen(d) - 1] == '.' || strstr(d, "..")) return 0;
    for (; *d; d++) if (!isalnum((unsigned char)*d) && *d != '.' && *d != '-') return 0;
    return 1;
}

int is_college_email(const char *e) {
    const char *at = strchr(e, '@');
    return at && at != e && strcmp(at + 1, COLLEGE_DOMAIN) == 0;
}

int valid_password(const char *pw, const char **why) {
    size_t n = strlen(pw), i;
    int letter = 0, digit = 0;
    if (n < 8) { *why = "Password must be at least 8 characters."; return 0; }
    if (n > 128) { *why = "Password must be at most 128 characters."; return 0; }
    for (i = 0; i < n; i++) { letter |= isalpha((unsigned char)pw[i]) != 0; digit |= isdigit((unsigned char)pw[i]) != 0; }
    if (!letter || !digit) { *why = "Password must contain at least one letter and one number."; return 0; }
    return 1;
}

int valid_full_name(const char *s) {
    size_t n = strlen(s), i;
    if (n < 2 || n > 100) return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f || c == '<' || c == '>') return 0;
    }
    return 1;
}

int valid_enrollment(char *s) {
    size_t n = strlen(s), i;
    if (n < 4 || n > 20) return 0;
    for (i = 0; i < n; i++) {
        if (!isalnum((unsigned char)s[i]) && s[i] != '-') return 0;
        s[i] = (char)toupper((unsigned char)s[i]);
    }
    return 1;
}
