/* init_admin: creates the SQLite database (schema + 39 clubs) if needed and seeds the
   administrator account. Run from the backend/ folder. The password is read from
   config/admin.conf (private, never served to the browser) and only a PBKDF2 hash is stored. */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "password.h"
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0700)
#endif

#define WEAK_PASSWORD_LEN 12

typedef struct { char email[256], password[256], db[512], schema[512], seed[512]; } Config;

static void wipe(void *p, size_t n) { volatile unsigned char *v = p; while (n--) *v++ = 0; }

static char *trim(char *s) {
    char *e;
    while (isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static void set(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src); }

static void load_config(Config *c, const char *path, int *found) {
    FILE *f = fopen(path, "r");
    char line[1024];
    *found = f != NULL;
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *k = trim(line), *eq, *v;
        if (*k == '#' || *k == '\0' || !(eq = strchr(k, '='))) continue;
        *eq = '\0'; v = trim(eq + 1); k = trim(k);
        if (!strcmp(k, "ADMIN_EMAIL")) set(c->email, sizeof c->email, v);
        else if (!strcmp(k, "ADMIN_PASSWORD")) set(c->password, sizeof c->password, v);
        else if (!strcmp(k, "DB_PATH")) set(c->db, sizeof c->db, v);
        else if (!strcmp(k, "SCHEMA_PATH")) set(c->schema, sizeof c->schema, v);
        else if (!strcmp(k, "SEED_PATH")) set(c->seed, sizeof c->seed, v);
    }
    fclose(f);
    wipe(line, sizeof line);
}

static int valid_email(char *e) {
    size_t i, n = strlen(e);
    const char *at = strchr(e, '@'), *dot;
    if (n < 5 || n > 254 || !at || at == e || strchr(at + 1, '@')) return 0;
    dot = strrchr(at, '.');
    if (!dot || dot == at + 1 || dot[1] == '\0') return 0;
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)e[i];
        if (ch <= 32 || ch >= 127 || ch == ',' || ch == ';' || ch == '\'' || ch == '"') return 0;
        e[i] = (char)tolower(ch);
    }
    return 1;
}

static void ensure_parent_dir(const char *path) {
    char tmp[512];
    char *s;
    set(tmp, sizeof tmp, path);
    s = strrchr(tmp, '/');
    { char *b = strrchr(tmp, '\\'); if (b > s) s = b; }
    if (s) { *s = '\0'; MKDIR(tmp); }
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = '\0';
    fclose(f);
    return buf;
}

static int exec_sql(sqlite3 *db, const char *sql, const char *what) {
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "ERROR: %s failed: %s\n", what, err ? err : "unknown");
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

static int exec_file(sqlite3 *db, const char *path, int transaction) {
    char *sql = read_file(path);
    int rc;
    if (!sql) { fprintf(stderr, "ERROR: cannot read %s\n", path); return -1; }
    if (transaction) exec_sql(db, "BEGIN;", "begin");
    rc = exec_sql(db, sql, path);
    if (transaction) exec_sql(db, rc == 0 ? "COMMIT;" : "ROLLBACK;", "end transaction");
    free(sql);
    return rc;
}

static int query_int(sqlite3 *db, const char *sql) {
    sqlite3_stmt *s = NULL;
    int v = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &s, NULL) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW)
        v = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    return v;
}

static int run_stmt(sqlite3 *db, const char *sql, const char *t1, const char *t2, const char *t3, int id) {
    sqlite3_stmt *s = NULL;
    int rc;
    if (sqlite3_prepare_v2(db, sql, -1, &s, NULL) != SQLITE_OK) { fprintf(stderr, "ERROR: %s\n", sqlite3_errmsg(db)); return -1; }
    if (t1) sqlite3_bind_text(s, 1, t1, -1, SQLITE_TRANSIENT);
    if (t2) sqlite3_bind_text(s, 2, t2, -1, SQLITE_TRANSIENT);
    if (t3) sqlite3_bind_text(s, 3, t3, -1, SQLITE_TRANSIENT);
    if (id >= 0) sqlite3_bind_int(s, 4, id);
    rc = sqlite3_step(s);
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) { fprintf(stderr, "ERROR: %s\n", sqlite3_errmsg(db)); return -1; }
    return 0;
}

static void usage(void) {
    puts("Usage: init_admin [--config FILE] [--reset] [--production]\n"
         "  Creates the database if missing (schema + 39 clubs) and seeds the administrator.\n"
         "  --reset       replace the existing administrator's email and password\n"
         "  --production  refuse passwords shorter than 12 characters (use before real deployment)");
}

int main(int argc, char **argv) {
    Config c;
    const char *cfg_path = "config/admin.conf";
    int reset = 0, production = 0, found, i, rc = 1;
    char hash[PASSWORD_HASH_MAX], *env;
    sqlite3 *db = NULL;

    memset(&c, 0, sizeof c);
    set(c.db, sizeof c.db, "../data/unisphere.db");
    set(c.schema, sizeof c.schema, "../database/schema.sql");
    set(c.seed, sizeof c.seed, "../database/seed.sql");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--reset")) reset = 1;
        else if (!strcmp(argv[i], "--production")) production = 1;
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) cfg_path = argv[++i];
        else { usage(); return strcmp(argv[i], "--help") ? 2 : 0; }
    }
    load_config(&c, cfg_path, &found);
    if ((env = getenv("UNISPHERE_ADMIN_EMAIL")) != NULL) set(c.email, sizeof c.email, env);
    if ((env = getenv("UNISPHERE_ADMIN_PASSWORD")) != NULL) set(c.password, sizeof c.password, env);
    if (!found && !c.email[0]) {
        fprintf(stderr, "ERROR: %s not found. Copy config/admin.conf.example to config/admin.conf and edit it.\n", cfg_path);
        return 1;
    }
    if (!valid_email(c.email)) { fprintf(stderr, "ERROR: ADMIN_EMAIL is missing or not a valid email address.\n"); goto done; }
    if (!c.password[0]) {
        printf("Administrator password (visible while typing): ");
        fflush(stdout);
        if (!fgets(c.password, sizeof c.password, stdin)) goto done;
        set(c.password, sizeof c.password, trim(c.password));
    }
    if (strlen(c.password) == 0 || strlen(c.password) > 128) { fprintf(stderr, "ERROR: password must be 1-128 characters.\n"); goto done; }
    if (strlen(c.password) < WEAK_PASSWORD_LEN) {
        if (production) { fprintf(stderr, "ERROR: --production requires a password of at least %d characters.\n", WEAK_PASSWORD_LEN); goto done; }
        fprintf(stderr, "WARNING: weak password. Acceptable for local development ONLY; replace it before any public deployment.\n");
    }

    ensure_parent_dir(c.db);
    if (sqlite3_open(c.db, &db) != SQLITE_OK) { fprintf(stderr, "ERROR: cannot open database %s\n", c.db); goto done; }
    if (exec_sql(db, "PRAGMA foreign_keys = ON;", "pragma")) goto done;
    if (query_int(db, "SELECT count(*) FROM sqlite_master WHERE name='clubs'") == 0) {
        if (exec_file(db, c.schema, 0)) goto done;
        puts("Database schema created.");
    }
    if (query_int(db, "SELECT count(*) FROM clubs") == 0) {
        if (exec_file(db, c.seed, 1)) goto done;
    }
    printf("Clubs in database: %d\n", query_int(db, "SELECT count(*) FROM clubs"));

    {
        int existing = query_int(db, "SELECT id FROM users WHERE role='admin' ORDER BY id LIMIT 1");
        if (existing > 0 && !reset) {
            puts("Administrator already exists. Nothing changed. Use --reset to replace the email/password.");
            rc = 0; goto done;
        }
        if (password_hash(c.password, hash, sizeof hash) != 0) { fprintf(stderr, "ERROR: could not hash password.\n"); goto done; }
        if (exec_sql(db, "BEGIN;", "begin")) goto done;
        if (existing > 0) {
            if (run_stmt(db, "UPDATE users SET email=?1, password_hash=?2, updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?4",
                         c.email, hash, NULL, existing) ||
                run_stmt(db, "DELETE FROM sessions WHERE user_id=?4", NULL, NULL, NULL, existing) ||
                run_stmt(db, "INSERT INTO audit_logs(user_id, action, target) VALUES(?4, 'admin_reset', 'init_admin')", NULL, NULL, NULL, existing)) {
                exec_sql(db, "ROLLBACK;", "rollback"); goto done;
            }
            puts("Administrator account updated (old sessions revoked).");
        } else {
            if (run_stmt(db, "INSERT INTO users(full_name, email, password_hash, role, email_verified) VALUES('Administrator', ?1, ?2, 'admin', 0)",
                         c.email, hash, NULL, -1) ||
                run_stmt(db, "INSERT INTO audit_logs(user_id, action, target) VALUES(last_insert_rowid(), 'admin_created', 'init_admin')", NULL, NULL, NULL, -1)) {
                exec_sql(db, "ROLLBACK;", "rollback"); goto done;
            }
            puts("Administrator account created.");
        }
        if (exec_sql(db, "COMMIT;", "commit")) goto done;
    }
    printf("Administrator email: %s\nDatabase: %s\n", c.email, c.db);
    puts("Reminder: delete the ADMIN_PASSWORD line from config/admin.conf now that it is hashed.");
    rc = 0;
done:
    if (db) sqlite3_close(db);
    wipe(&c, sizeof c);
    wipe(hash, sizeof hash);
    return rc;
}
