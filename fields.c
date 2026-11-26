/* Table-driven validation + INSERT/UPDATE building (all values are bound parameters). */
#include "fields.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

static void trim_in_place(char *s) {
    size_t l = strlen(s), st = 0;
    while (s[st] == ' ' || s[st] == '\t' || s[st] == '\r' || s[st] == '\n') st++;
    memmove(s, s + st, l - st + 1);
    l = strlen(s);
    while (l && (s[l-1] == ' ' || s[l-1] == '\t' || s[l-1] == '\r' || s[l-1] == '\n')) s[--l] = '\0';
}

static int check_value(const Field *f, char *v) {
    size_t n = strlen(v), i;
    switch (f->kind) {
    case F_TEXT:
        if ((int)n < f->min || (int)n > f->max) return 0;
        for (i = 0; i < n; i++) {
            unsigned char ch = (unsigned char)v[i];
            if (ch == 0x7f || ch == '<' || ch == '>') return 0;
            if (ch < 0x20 && !((ch == '\n' || ch == '\t') && f->max > 200)) return 0;
        }
        return 1;
    case F_ENUM: return in_list(v, f->list);
    case F_DATE: return valid_date(v);
    case F_TIME: return valid_time(v);
    case F_INT: {
        long x;
        if (n == 0 || n > 9) return 0;
        for (i = 0; i < n; i++) if (!isdigit((unsigned char)v[i])) return 0;
        x = atol(v);
        return x >= f->min && x <= f->max;
    }
    case F_BOOL:
        if (!strcmp(v, "true") || !strcmp(v, "1")) { strcpy(v, "1"); return 1; }
        if (!strcmp(v, "false") || !strcmp(v, "0")) { strcpy(v, "0"); return 1; }
        return 0;
    case F_IMAGE:
        if ((int)n > f->max || strncmp(v, "assets/images/", 14) != 0 || strstr(v, "..") || strstr(v, "//")) return 0;
        for (i = 0; i < n; i++) if (!isalnum((unsigned char)v[i]) && !strchr("._/-", v[i])) return 0;
        return 1;
    case F_HANDLE: {
        char tmp[64];
        const char *h = v + (v[0] == '@');
        size_t hl = strlen(h);
        if (hl < 1 || hl > 30) return 0;
        for (i = 0; i < hl; i++) if (!isalnum((unsigned char)h[i]) && h[i] != '.' && h[i] != '_') return 0;
        snprintf(tmp, sizeof tmp, "@%s", h);
        strcpy(v, tmp);
        return 1;
    }
    }
    return 0;
}

int fields_validate(Ctx *c, JObj *o, const Field *fs, int nf, int creating, char (*out)[FIELD_MAX], int *present, int *isnull) {
    int i;
    char msg[160];
    for (i = 0; i < nf; i++) {
        const char *v = json_get(o, fs[i].name);
        present[i] = 0; isnull[i] = 0; out[i][0] = '\0';
        if (!v) {
            if (creating && fs[i].required) { snprintf(msg, sizeof msg, "%s is required.", fs[i].label); api_error(c, 400, "missing_field", msg); return 0; }
            continue;
        }
        if (strlen(v) >= FIELD_MAX) { snprintf(msg, sizeof msg, "%s is too long.", fs[i].label); api_error(c, 400, "invalid_field", msg); return 0; }
        strcpy(out[i], v);
        trim_in_place(out[i]);
        if (out[i][0] == '\0') {
            if (fs[i].nullable) { present[i] = 1; isnull[i] = 1; continue; }
            if (fs[i].kind == F_TEXT && fs[i].min == 0) { present[i] = 1; continue; }
            snprintf(msg, sizeof msg, "%s cannot be empty.", fs[i].label); api_error(c, 400, "invalid_field", msg); return 0;
        }
        if (!check_value(&fs[i], out[i])) { snprintf(msg, sizeof msg, "%s is invalid.", fs[i].label); api_error(c, 400, "invalid_field", msg); return 0; }
        present[i] = 1;
    }
    return 1;
}

const char *fields_val(const Field *fs, int nf, char (*out)[FIELD_MAX], const int *present, const int *isnull, const char *name) {
    int i;
    for (i = 0; i < nf; i++) if (!strcmp(fs[i].name, name)) return present[i] && !isnull[i] ? out[i] : NULL;
    return NULL;
}

static int bind_run(sqlite3 *db, Buf *sql, const Field *fs, int nf, char (*out)[FIELD_MAX], const int *present, const int *isnull, int id) {
    sqlite3_stmt *st = NULL;
    int i, k = 1, rc;
    (void)fs;
    if (sqlite3_prepare_v2(db, sql->d, -1, &st, NULL) != SQLITE_OK) return -1;
    for (i = 0; i < nf; i++) {
        if (!present[i]) continue;
        if (isnull[i]) sqlite3_bind_null(st, k++); else sqlite3_bind_text(st, k++, out[i], -1, SQLITE_TRANSIENT);
    }
    if (id > 0) sqlite3_bind_int(st, k, id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int fields_insert(sqlite3 *db, const char *table, const Field *fs, int nf, char (*out)[FIELD_MAX], const int *present, const int *isnull) {
    Buf cols, vals; int i, n = 0, rc;
    Buf sql;
    buf_init(&cols); buf_init(&vals); buf_init(&sql);
    for (i = 0; i < nf; i++) {
        if (!present[i]) continue;
        buf_puts(&cols, n ? "," : ""); buf_puts(&cols, fs[i].column);
        buf_puts(&vals, n ? ",?" : "?");
        n++;
    }
    if (n == 0) { buf_free(&cols); buf_free(&vals); return -1; }
    buf_printf(&sql, "INSERT INTO %s(", table); buf_puts(&sql, cols.d); buf_puts(&sql, ") VALUES("); buf_puts(&sql, vals.d); buf_puts(&sql, ")");
    rc = bind_run(db, &sql, fs, nf, out, present, isnull, 0);
    buf_free(&cols); buf_free(&vals); buf_free(&sql);
    return rc;
}

int fields_update(sqlite3 *db, const char *table, int id, const Field *fs, int nf, char (*out)[FIELD_MAX], const int *present, const int *isnull) {
    Buf sql; int i, rc;
    buf_init(&sql);
    buf_printf(&sql, "UPDATE %s SET ", table);
    for (i = 0; i < nf; i++) if (present[i]) { buf_puts(&sql, fs[i].column); buf_puts(&sql, "=?,"); }
    buf_puts(&sql, "updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?");
    rc = bind_run(db, &sql, fs, nf, out, present, isnull, id);
    buf_free(&sql);
    return rc;
}
