#include "json.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void buf_init(Buf *b) { b->d = NULL; b->n = b->cap = 0; }
void buf_free(Buf *b) { free(b->d); buf_init(b); }
static int buf_add(Buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        char *p;
        while (nc < b->n + n + 1) nc *= 2;
        p = realloc(b->d, nc);
        if (!p) return -1;
        b->d = p; b->cap = nc;
    }
    memcpy(b->d + b->n, s, n);
    b->n += n; b->d[b->n] = '\0';
    return 0;
}
int buf_puts(Buf *b, const char *s) { return buf_add(b, s, strlen(s)); }
int buf_printf(Buf *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    int n;
    va_start(ap, fmt); n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (n < 0 || (size_t)n >= sizeof tmp) return -1;
    return buf_add(b, tmp, (size_t)n);
}
void buf_json_str(Buf *b, const char *s) {
    buf_add(b, "\"", 1);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"') buf_add(b, "\\\"", 2);
        else if (c == '\\') buf_add(b, "\\\\", 2);
        else if (c == '\n') buf_add(b, "\\n", 2);
        else if (c == '\r') buf_add(b, "\\r", 2);
        else if (c == '\t') buf_add(b, "\\t", 2);
        else if (c < 0x20) { char t[8]; snprintf(t, sizeof t, "\\u%04x", c); buf_add(b, t, 6); }
        else buf_add(b, (const char *)&c, 1);
    }
    buf_add(b, "\"", 1);
}

/* ---- parser ---- */
static void ws(const char **p) { while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++; }
static int hex4(const char *p, unsigned *out) {
    unsigned v = 0; int i;
    for (i = 0; i < 4; i++) {
        char c = p[i]; v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = v; return 0;
}
static void utf8(Buf *b, unsigned cp) {
    char t[4]; int n;
    if (cp < 0x80) { t[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { t[0] = (char)(0xC0 | cp >> 6); t[1] = (char)(0x80 | (cp & 63)); n = 2; }
    else if (cp < 0x10000) { t[0] = (char)(0xE0 | cp >> 12); t[1] = (char)(0x80 | (cp >> 6 & 63)); t[2] = (char)(0x80 | (cp & 63)); n = 3; }
    else { t[0] = (char)(0xF0 | cp >> 18); t[1] = (char)(0x80 | (cp >> 12 & 63)); t[2] = (char)(0x80 | (cp >> 6 & 63)); t[3] = (char)(0x80 | (cp & 63)); n = 4; }
    buf_add(b, t, (size_t)n);
}
static int parse_string(const char **pp, char **out) {
    const char *p = *pp;
    Buf b; buf_init(&b);
    if (*p != '"') return -1;
    p++;
    for (;;) {
        unsigned char c = (unsigned char)*p;
        if (c == '\0' || c < 0x20) goto bad;
        if (b.n > JSON_MAX_STRING) goto bad;
        if (c == '"') { p++; break; }
        if (c == '\\') {
            p++;
            switch (*p) {
            case '"': buf_add(&b, "\"", 1); break; case '\\': buf_add(&b, "\\", 1); break;
            case '/': buf_add(&b, "/", 1); break; case 'b': buf_add(&b, "\b", 1); break;
            case 'f': buf_add(&b, "\f", 1); break; case 'n': buf_add(&b, "\n", 1); break;
            case 'r': buf_add(&b, "\r", 1); break; case 't': buf_add(&b, "\t", 1); break;
            case 'u': {
                unsigned cp, lo;
                if (hex4(p + 1, &cp) != 0) goto bad;
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (p[1] != '\\' || p[2] != 'u' || hex4(p + 3, &lo) != 0 || lo < 0xDC00 || lo > 0xDFFF) goto bad;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) goto bad;
                if (cp == 0) goto bad;
                utf8(&b, cp);
                break; }
            default: goto bad;
            }
            p++;
        } else { buf_add(&b, (const char *)&c, 1); p++; }
    }
    if (!b.d) { b.d = calloc(1, 1); if (!b.d) return -1; }
    *out = b.d; *pp = p;
    return 0;
bad:
    buf_free(&b);
    return -1;
}

void json_free(JObj *o) { int i; for (i = 0; i < o->n; i++) free(o->f[i].val); o->n = 0; }

int json_parse_flat(const char *s, JObj *o) {
    const char *p = s;
    o->n = 0;
    ws(&p);
    if (*p++ != '{') return -1;
    ws(&p);
    if (*p == '}') { p++; goto tail; }
    for (;;) {
        char *key = NULL, *val = NULL;
        ws(&p);
        if (parse_string(&p, &key) != 0) goto fail;
        ws(&p);
        if (*p++ != ':') { free(key); goto fail; }
        ws(&p);
        if (*p == '"') {
            if (parse_string(&p, &val) != 0) { free(key); goto fail; }
        } else if (*p == '{' || *p == '[' || *p == '\0') { free(key); goto fail; }
        else {
            const char *st = p; size_t n;
            while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
            n = (size_t)(p - st);
            if (n == 0 || n > 32) { free(key); goto fail; }
            val = malloc(n + 1);
            if (!val) { free(key); goto fail; }
            memcpy(val, st, n); val[n] = '\0';
            if (!strcmp(val, "null")) { free(val); val = NULL; }
            else if (strcmp(val, "true") && strcmp(val, "false") && strspn(val, "0123456789+-.eE") != n) { free(key); free(val); goto fail; }
        }
        if (strlen(key) >= sizeof o->f[0].key || o->n >= JSON_MAX_FIELDS) { free(key); free(val); goto fail; }
        if (val) { strcpy(o->f[o->n].key, key); o->f[o->n].val = val; o->n++; }
        free(key);
        ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; break; }
        goto fail;
    }
tail:
    ws(&p);
    if (*p != '\0') goto fail;
    return 0;
fail:
    json_free(o);
    return -1;
}

const char *json_get(const JObj *o, const char *key) {
    int i;
    for (i = 0; i < o->n; i++) if (!strcmp(o->f[i].key, key)) return o->f[i].val;
    return NULL;
}
