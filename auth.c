/* Authentication, sessions, CSRF and login rate limiting. */
#include "auth.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "password.h"
#include "sha256.h"
#include "validate.h"

#define COOKIE_NAME "unisphere_sid"
#define STUDENT_TTL 28800      /* 8 hours */
#define ADMIN_TTL   7200       /* 2 hours */
#define MAX_FAIL_EMAIL 5
#define MAX_FAIL_IP    20
#define FAIL_WINDOW    900     /* 15 minutes */

static char dummy_hash[PASSWORD_HASH_MAX];

void auth_init(void) { password_hash("dummy-password-for-timing", dummy_hash, sizeof dummy_hash); }

static void sha256_hex(const char *s, char out[65]) {
    uint8_t d[32]; sha256_ctx c; int i;
    sha256_init(&c); sha256_update(&c, s, strlen(s)); sha256_final(&c, d);
    for (i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", d[i]);
}

static int rand_hex(char *out, size_t bytes) {
    uint8_t b[32]; size_t i;
    if (bytes > sizeof b || random_bytes(b, bytes) != 0) return -1;
    for (i = 0; i < bytes; i++) sprintf(out + 2 * i, "%02x", b[i]);
    return 0;
}

static int get_sid(const char *cookie, char out[65]) {
    const char *p = cookie;
    size_t k = strlen(COOKIE_NAME "=");
    while (p && *p) {
        while (*p == ' ') p++;
        if (strncmp(p, COOKIE_NAME "=", k) == 0) {
            size_t i = 0;
            p += k;
            while (i < 64 && ((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f'))) i++;
            if (i == 64 && (p[64] == '\0' || p[64] == ';')) { memcpy(out, p, 64); out[64] = '\0'; return 1; }
            return 0;
        }
        p = strchr(p, ';');
        if (p) p++;
    }
    return 0;
}

int auth_current(Ctx *c, Session *s) {
    char sid[65], hash[65];
    sqlite3_stmt *st = NULL;
    int ok = 0;
    if (!get_sid(c->req->cookie, sid)) return 0;
    sha256_hex(sid, hash);
    if (sqlite3_prepare_v2(c->db,
        "SELECT u.id,u.role,u.full_name,u.email,COALESCE(u.enrollment_no,''),s.csrf_token "
        "FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.token_hash=?1 AND s.expires_at>?2",
        -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, (long long)time(NULL));
    if (sqlite3_step(st) == SQLITE_ROW) {
        s->user_id = sqlite3_column_int(st, 0);
        snprintf(s->role, sizeof s->role, "%s", (const char *)sqlite3_column_text(st, 1));
        snprintf(s->name, sizeof s->name, "%s", (const char *)sqlite3_column_text(st, 2));
        snprintf(s->email, sizeof s->email, "%s", (const char *)sqlite3_column_text(st, 3));
        snprintf(s->enrollment, sizeof s->enrollment, "%s", (const char *)sqlite3_column_text(st, 4));
        snprintf(s->csrf, sizeof s->csrf, "%s", (const char *)sqlite3_column_text(st, 5));
        ok = 1;
    }
    sqlite3_finalize(st);
    return ok;
}

int auth_csrf_ok(const Ctx *c, const Session *s) {
    const char *a = c->req->csrf, *b = s->csrf;
    unsigned char diff = 0;
    size_t i, n = strlen(b);
    if (strlen(a) != n || n == 0) return 0;
    for (i = 0; i < n; i++) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

static int exec_params(sqlite3 *db, const char *sql, const char *t1, long long i2) {
    sqlite3_stmt *st = NULL; int rc;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    if (t1) sqlite3_bind_text(st, 1, t1, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, i2);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

/* Creates a session row (only the SHA-256 of the token is stored) and the Set-Cookie header. */
static int create_session(Ctx *c, int uid, const char *role, char *cookie_hdr, size_t sz, char csrf[33]) {
    char token[65], hash[65];
    long long now = (long long)time(NULL);
    int ttl = strcmp(role, "admin") == 0 ? ADMIN_TTL : STUDENT_TTL;
    sqlite3_stmt *st = NULL; int rc;
    if (rand_hex(token, 32) != 0 || rand_hex(csrf, 16) != 0) return -1;
    sha256_hex(token, hash);
    exec_params(c->db, "DELETE FROM sessions WHERE expires_at<=?2", NULL, now);
    if (sqlite3_prepare_v2(c->db, "INSERT INTO sessions(token_hash,user_id,csrf_token,created_at,expires_at) VALUES(?1,?2,?3,?4,?5)", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, uid);
    sqlite3_bind_text(st, 3, csrf, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, now);
    sqlite3_bind_int64(st, 5, now + ttl);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;
    snprintf(cookie_hdr, sz, "Set-Cookie: " COOKIE_NAME "=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%d%s\r\n",
             token, ttl, c->secure_cookie ? "; Secure" : "");
    return 0;
}

static void user_json(Buf *b, int id, const char *name, const char *email, const char *role, const char *enr) {
    buf_printf(b, "{\"id\":%d,\"full_name\":", id); buf_json_str(b, name);
    buf_puts(b, ",\"email\":"); buf_json_str(b, email);
    buf_puts(b, ",\"role\":"); buf_json_str(b, role);
    buf_puts(b, ",\"enrollment_no\":"); if (enr && *enr) buf_json_str(b, enr); else buf_puts(b, "null");
    buf_puts(b, ",\"email_verified\":false}");   /* domain is checked, ownership is NOT verified */
}

static void send_session_response(Ctx *c, int status, int uid, const char *name, const char *email,
                                  const char *role, const char *enr) {
    char hdr[300], csrf[33];
    Buf b; buf_init(&b);
    if (create_session(c, uid, role, hdr, sizeof hdr, csrf) != 0) { api_error(c, 500, "server_error", "Could not create session."); return; }
    buf_puts(&b, "{\"ok\":true,\"user\":"); user_json(&b, uid, name, email, role, enr);
    buf_printf(&b, ",\"csrf_token\":\"%s\"}", csrf);
    api_send(c, status, b.d, hdr);
    buf_free(&b);
}

/* ---- rate limiting ---- */
static int count_fail(sqlite3 *db, const char *ident, long long since) {
    sqlite3_stmt *st = NULL; int n = 0;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM login_attempts WHERE identifier=?1 AND attempted_at>?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, ident, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, since);
        if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return n;
}
static int locked_out(Ctx *c, const char *email) {
    char eid[300], iid[100];
    long long since = (long long)time(NULL) - FAIL_WINDOW;
    snprintf(eid, sizeof eid, "email:%s", email);
    snprintf(iid, sizeof iid, "ip:%s", c->req->client_ip);
    return count_fail(c->db, eid, since) >= MAX_FAIL_EMAIL || count_fail(c->db, iid, since) >= MAX_FAIL_IP;
}
static void record_fail(Ctx *c, const char *email) {
    char eid[300], iid[100];
    long long now = (long long)time(NULL);
    snprintf(eid, sizeof eid, "email:%s", email);
    snprintf(iid, sizeof iid, "ip:%s", c->req->client_ip);
    exec_params(c->db, "INSERT INTO login_attempts(identifier,attempted_at) VALUES(?1,?2)", eid, now);
    exec_params(c->db, "INSERT INTO login_attempts(identifier,attempted_at) VALUES(?1,?2)", iid, now);
    exec_params(c->db, "DELETE FROM login_attempts WHERE attempted_at<?2", NULL, now - 86400);
}

static void do_login(Ctx *c, int admin) {
    JObj o;
    const char *pw;
    char email[260];
    sqlite3_stmt *st = NULL;
    int uid = 0, good = 0;
    char name[128] = "", role[8] = "", enr[32] = "", stored[PASSWORD_HASH_MAX] = "";
    const char *raw;

    if (!api_read_json(c, &o)) return;
    raw = json_get(&o, "email"); pw = json_get(&o, "password");
    if (!raw || !pw || !*pw || strlen(raw) > 254 || strlen(pw) > 128) {
        json_free(&o); api_error(c, 400, "missing_fields", "Email and password are required."); return;
    }
    snprintf(email, sizeof email, "%s", raw);
    if (!valid_email_format(email)) { json_free(&o); api_error(c, 400, "invalid_email", "Enter a valid email address."); return; }
    if (!admin && !is_college_email(email)) {
        json_free(&o); api_error(c, 400, "invalid_domain", "Students must use an email ending in @" COLLEGE_DOMAIN "."); return;
    }
    if (locked_out(c, email)) {
        json_free(&o); api_error(c, 429, "too_many_attempts", "Too many failed attempts. Try again in 15 minutes."); return;
    }
    if (sqlite3_prepare_v2(c->db, "SELECT id,password_hash,role,full_name,COALESCE(enrollment_no,'') FROM users WHERE email=?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, email, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            uid = sqlite3_column_int(st, 0);
            snprintf(stored, sizeof stored, "%s", (const char *)sqlite3_column_text(st, 1));
            snprintf(role, sizeof role, "%s", (const char *)sqlite3_column_text(st, 2));
            snprintf(name, sizeof name, "%s", (const char *)sqlite3_column_text(st, 3));
            snprintf(enr, sizeof enr, "%s", (const char *)sqlite3_column_text(st, 4));
        }
    }
    sqlite3_finalize(st);
    if (uid && strcmp(role, admin ? "admin" : "student") == 0) good = password_verify(pw, stored);
    else password_verify(pw, dummy_hash);              /* same work whether or not the account exists */
    if (!good) {
        record_fail(c, email);
        json_free(&o);
        api_error(c, 401, "invalid_credentials", "Invalid email or password.");
        return;
    }
    { char eid[300]; snprintf(eid, sizeof eid, "email:%s", email);
      exec_params(c->db, "DELETE FROM login_attempts WHERE identifier=?1 AND attempted_at>?2", eid, 0); }
    json_free(&o);
    send_session_response(c, 200, uid, name, email, role, enr);
}

void auth_student_login(Ctx *c) { do_login(c, 0); }
void auth_admin_login(Ctx *c) { do_login(c, 1); }

void auth_student_register(Ctx *c) {
    JObj o;
    const char *rn, *re, *rp, *rp2, *rs;
    char name[128], email[260], enr[40] = "", pw_hash[PASSWORD_HASH_MAX];
    const char *why = NULL;
    sqlite3_stmt *st = NULL;
    int rc, uid;

    if (!api_read_json(c, &o)) return;
    /* any "role" field sent by the client is deliberately ignored: students are always role 'student' */
    rn = json_get(&o, "full_name"); re = json_get(&o, "email"); rp = json_get(&o, "password");
    rp2 = json_get(&o, "confirm_password"); rs = json_get(&o, "enrollment_no");
    if (!rn || !re || !rp || !rp2) { json_free(&o); api_error(c, 400, "missing_fields", "Name, email, password and confirmation are required."); return; }
    if (strlen(rn) > 100 || strlen(re) > 254) { json_free(&o); api_error(c, 400, "invalid_input", "Input too long."); return; }
    snprintf(name, sizeof name, "%s", rn);
    snprintf(email, sizeof email, "%s", re);
    { char *s = name; size_t l; while (*s == ' ') s++; memmove(name, s, strlen(s) + 1); l = strlen(name); while (l && name[l-1] == ' ') name[--l] = '\0'; }
    if (!valid_full_name(name)) { json_free(&o); api_error(c, 400, "invalid_name", "Enter your full name (2-100 characters, no < or >)."); return; }
    if (!valid_email_format(email)) { json_free(&o); api_error(c, 400, "invalid_email", "Enter a valid email address."); return; }
    if (!is_college_email(email)) { json_free(&o); api_error(c, 400, "invalid_domain", "Students must register with an email ending in @" COLLEGE_DOMAIN "."); return; }
    if (rs && *rs) {
        if (strlen(rs) > 20) { json_free(&o); api_error(c, 400, "invalid_enrollment", "Invalid enrollment number."); return; }
        snprintf(enr, sizeof enr, "%s", rs);
        if (!valid_enrollment(enr)) { json_free(&o); api_error(c, 400, "invalid_enrollment", "Enrollment number must be 4-20 letters, digits or hyphens."); return; }
    }
    if (!valid_password(rp, &why)) { json_free(&o); api_error(c, 400, "weak_password", why); return; }
    if (strcmp(rp, rp2) != 0) { json_free(&o); api_error(c, 400, "password_mismatch", "Passwords do not match."); return; }
    if (password_hash(rp, pw_hash, sizeof pw_hash) != 0) { json_free(&o); api_error(c, 500, "server_error", "Could not process password."); return; }
    json_free(&o);

    if (sqlite3_prepare_v2(c->db, "INSERT INTO users(full_name,email,enrollment_no,password_hash,role) VALUES(?1,?2,?3,?4,'student')", -1, &st, NULL) != SQLITE_OK) {
        api_error(c, 500, "server_error", "Database error."); return;
    }
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, email, -1, SQLITE_TRANSIENT);
    if (enr[0]) sqlite3_bind_text(st, 3, enr, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st, 3);
    sqlite3_bind_text(st, 4, pw_hash, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        if (strstr(sqlite3_errmsg(c->db), "UNIQUE")) api_error(c, 409, "already_registered", "An account with this email or enrollment number already exists.");
        else api_error(c, 500, "server_error", "Could not create account.");
        return;
    }
    uid = (int)sqlite3_last_insert_rowid(c->db);
    send_session_response(c, 201, uid, name, email, "student", enr);
}

#define CLEAR_COOKIE "Set-Cookie: " COOKIE_NAME "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0\r\n"

void auth_logout(Ctx *c) {
    Session s; char sid[65], hash[65];
    if (!api_origin_ok(c->req)) { api_error(c, 403, "bad_origin", "Cross-site request blocked."); return; }
    if (auth_current(c, &s)) {
        if (!auth_csrf_ok(c, &s)) { api_error(c, 403, "bad_csrf", "Missing or invalid CSRF token."); return; }
        get_sid(c->req->cookie, sid); sha256_hex(sid, hash);
        exec_params(c->db, "DELETE FROM sessions WHERE token_hash=?1", hash, 0);
    }
    api_send(c, 200, "{\"ok\":true}", CLEAR_COOKIE);
}

void auth_me(Ctx *c) {
    Session s; Buf b;
    if (!auth_current(c, &s)) { api_error(c, 401, "not_authenticated", "Not logged in."); return; }
    buf_init(&b);
    buf_puts(&b, "{\"ok\":true,\"user\":"); user_json(&b, s.user_id, s.name, s.email, s.role, s.enrollment);
    buf_printf(&b, ",\"csrf_token\":\"%s\"}", s.csrf);
    api_send(c, 200, b.d, NULL);
    buf_free(&b);
}

/* Requires a logged-in user with the given role; state-changing requests also need Origin + CSRF. */
int auth_require(Ctx *c, Session *s, const char *role) {
    if (!auth_current(c, s)) { api_error(c, 401, "not_authenticated", "Please log in."); return 0; }
    if (strcmp(s->role, role) != 0) { api_error(c, 403, "forbidden", "You do not have permission to do that."); return 0; }
    if (strcmp(c->req->method, "GET") != 0) {
        if (!api_origin_ok(c->req)) { api_error(c, 403, "bad_origin", "Cross-site request blocked."); return 0; }
        if (!auth_csrf_ok(c, s)) { api_error(c, 403, "bad_csrf", "Missing or invalid CSRF token."); return 0; }
    }
    return 1;
}
