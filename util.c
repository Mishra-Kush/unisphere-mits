#include "util.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void ist_now(Now *n) {
    time_t t = time(NULL) + 19800;
    struct tm *g = gmtime(&t);
    strftime(n->today, sizeof n->today, "%Y-%m-%d", g);
    strftime(n->hm, sizeof n->hm, "%H:%M", g);
}

static int hv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

int qs_get(const char *q, const char *key, char *out, size_t n) {
    size_t kl = strlen(key);
    const char *p = q;
    out[0] = '\0';
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t len = amp ? (size_t)(amp - p) : strlen(p), i, o = 0;
        if (len > kl && p[kl] == '=' && strncmp(p, key, kl) == 0) {
            for (i = kl + 1; i < len && o + 1 < n; i++) {
                unsigned char c = (unsigned char)p[i];
                if (c == '+') c = ' ';
                else if (c == '%' && i + 2 < len + 0 + 1 && hv(p[i+1]) >= 0 && hv(p[i+2]) >= 0) { c = (unsigned char)(hv(p[i+1]) * 16 + hv(p[i+2])); i += 2; }
                if (c >= 0x20 && c != 0x7f) out[o++] = (char)c;
            }
            out[o] = '\0';
            return 1;
        }
        p = amp ? amp + 1 : p + len;
    }
    return 0;
}

int path_id(const char *path, const char *prefix, int *id, const char **rest) {
    size_t pl = strlen(prefix);
    const char *p;
    long v = 0;
    if (strncmp(path, prefix, pl) != 0) return 0;
    p = path + pl;
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); if (v > 1000000000L) return 0; p++; }
    *id = (int)v; *rest = p;
    return 1;
}

int valid_date(const char *s) {
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int i, y, m, d, mx;
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
    for (i = 0; i < 10; i++) if (i != 4 && i != 7 && !isdigit((unsigned char)s[i])) return 0;
    y = atoi(s); m = atoi(s + 5); d = atoi(s + 8);
    if (y < 2000 || y > 2100 || m < 1 || m > 12) return 0;
    mx = dim[m - 1] + (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    return d >= 1 && d <= mx;
}

int valid_time(const char *s) {
    if (strlen(s) != 5 || s[2] != ':' || !isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) || !isdigit((unsigned char)s[3]) || !isdigit((unsigned char)s[4])) return 0;
    return atoi(s) <= 23 && atoi(s + 3) <= 59;
}

void like_pattern(const char *in, char *out, size_t n) {
    size_t o = 0;
    out[o++] = '%';
    for (; *in && o + 3 < n; in++) {
        if (*in == '\\' || *in == '%' || *in == '_') out[o++] = '\\';
        out[o++] = *in;
    }
    out[o++] = '%'; out[o] = '\0';
}

void instagram_url(const char *h, char *out, size_t n) {
    if (*h == '@') h++;
    if (*h) snprintf(out, n, "https://www.instagram.com/%s/", h); else out[0] = '\0';
}

void slugify(const char *in, char *out, size_t n) {
    size_t o = 0; int dash = 0;
    for (; *in && o + 2 < n; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) && c < 128) { out[o++] = (char)tolower(c); dash = 0; }
        else if (!dash && o > 0) { out[o++] = '-'; dash = 1; }
    }
    if (o > 0 && out[o - 1] == '-') o--;
    out[o] = '\0';
    if (o == 0) snprintf(out, n, "club");
}

int in_list(const char *v, const char *const *list) {
    for (; *list; list++) if (strcmp(v, *list) == 0) return 1;
    return 0;
}

void audit_log(sqlite3 *db, int uid, const char *action, const char *target) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "INSERT INTO audit_logs(user_id,action,target) VALUES(?1,?2,?3)", -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_int(st, 1, uid);
    sqlite3_bind_text(st, 2, action, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, target, -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void kv_s(Buf *b, const char *k, const char *v) { buf_printf(b, ",\"%s\":", k); buf_json_str(b, v ? v : ""); }
void kv_sn(Buf *b, const char *k, const char *v) { if (v && *v) kv_s(b, k, v); else buf_printf(b, ",\"%s\":null", k); }
void kv_i(Buf *b, const char *k, long long v) { buf_printf(b, ",\"%s\":%lld", k, v); }
void kv_in(Buf *b, const char *k, long long v) { if (v > 0) kv_i(b, k, v); else buf_printf(b, ",\"%s\":null", k); }
void kv_b(Buf *b, const char *k, int v) { buf_printf(b, ",\"%s\":%s", k, v ? "true" : "false"); }

void q_init(Q *q, const char *base) { buf_init(&q->sql); buf_puts(&q->sql, base); q->n = 0; }
void q_param(Q *q, const char *val) { if (q->n < 16) snprintf(q->vals[q->n++], sizeof q->vals[0], "%s", val); }
void q_and(Q *q, const char *cond, const char *val) {
    buf_puts(&q->sql, " AND "); buf_puts(&q->sql, cond);
    if (val) q_param(q, val);
}
void q_like(Q *q, const char *cond, const char *pattern, int times) {
    int i;
    buf_puts(&q->sql, " AND "); buf_puts(&q->sql, cond);
    for (i = 0; i < times; i++) q_param(q, pattern);
}
sqlite3_stmt *q_prepare(Q *q, sqlite3 *db) {
    sqlite3_stmt *st = NULL;
    int i;
    if (!q->sql.d || sqlite3_prepare_v2(db, q->sql.d, -1, &st, NULL) != SQLITE_OK) { buf_free(&q->sql); return NULL; }
    for (i = 0; i < q->n; i++) sqlite3_bind_text(st, i + 1, q->vals[i], -1, SQLITE_TRANSIENT);
    buf_free(&q->sql);
    return st;
}
