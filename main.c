/* UNISPHERE server: routing, static files, and the main accept loop. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "api.h"
#include "auth.h"

static int ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static const char *mime_for(const char *p) {
    if (ends_with(p, ".html")) return "text/html; charset=utf-8";
    if (ends_with(p, ".css")) return "text/css; charset=utf-8";
    if (ends_with(p, ".js")) return "text/javascript; charset=utf-8";
    if (ends_with(p, ".svg")) return "image/svg+xml";
    if (ends_with(p, ".png")) return "image/png";
    if (ends_with(p, ".jpg") || ends_with(p, ".jpeg")) return "image/jpeg";
    if (ends_with(p, ".webp")) return "image/webp";
    if (ends_with(p, ".ico")) return "image/x-icon";
    return NULL;
}

/* Only top-level .html pages and files under /assets/ are ever served. Everything else
   (backend/, database/, data/, config, source code) is unreachable from the browser. */
static void serve_static(Ctx *c, const char *root) {
    char path[512], full[1100];
    const char *mime;
    FILE *f;
    long n;
    char *buf;
    const HttpReq *r = c->req;

    if (strcmp(r->method, "GET") != 0) { http_send(c->sock, 405, "text/plain", "Method Not Allowed", 18, "Allow: GET\r\n", 0); return; }
    {   const char *src = strcmp(r->path, "/") == 0 ? "/index.html" : r->path;
        size_t sl = strlen(src);
        if (sl >= sizeof path) { http_send(c->sock, 404, "text/plain", "Not Found", 9, NULL, 0); return; }
        memcpy(path, src, sl + 1);
    }
    if (strstr(path, "..") || strchr(path, '\\') || strchr(path, '%') || strstr(path, "//") ||
        !(mime = mime_for(path)) ||
        !(strncmp(path, "/assets/", 8) == 0 || (strchr(path + 1, '/') == NULL && ends_with(path, ".html")))) {
        http_send(c->sock, 404, "text/plain", "Not Found", 9, NULL, 0); return;
    }
    if (strlen(root) + strlen(path) + 1 > sizeof full) { http_send(c->sock, 404, "text/plain", "Not Found", 9, NULL, 0); return; }
    strcpy(full, root); strcat(full, path);
    f = fopen(full, "rb");
    if (!f) { http_send(c->sock, 404, "text/plain", "Not Found", 9, NULL, 0); return; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 8 * 1024 * 1024 || !(buf = malloc((size_t)n + 1))) { fclose(f); http_send(c->sock, 500, "text/plain", "Error", 5, NULL, 0); return; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) n = 0;
    fclose(f);
    http_send(c->sock, 200, mime, buf, (size_t)n, NULL, 0);
    free(buf);
}

void api_dispatch(Ctx *c);

static void route(Ctx *c, const char *root) {
    if (strncmp(c->req->path, "/api/", 5) != 0) serve_static(c, root);
    else api_dispatch(c);
}

int main(int argc, char **argv) {
    const char *db_path = "../data/unisphere.db", *root = "..", *bind_ip = "127.0.0.1", *sec;
    int port = 8080, i;
    sqlite3 *db = NULL;
    sock_t ls;
    FILE *probe;

    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--port")) port = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--db")) db_path = argv[i + 1];
        else if (!strcmp(argv[i], "--web")) root = argv[i + 1];
        else if (!strcmp(argv[i], "--bind")) bind_ip = argv[i + 1];
        else { fprintf(stderr, "Usage: unisphere_server [--port N] [--db FILE] [--web DIR] [--bind IP]\n"); return 2; }
    }
    if (port < 1 || port > 65535) { fprintf(stderr, "Invalid port.\n"); return 2; }
    if (!(probe = fopen(db_path, "rb"))) { fprintf(stderr, "Database %s not found. Run init-admin first.\n", db_path); return 1; }
    fclose(probe);
    if (sqlite3_open(db_path, &db) != SQLITE_OK) { fprintf(stderr, "Cannot open database.\n"); return 1; }
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
    auth_init();
    ls = http_listen(bind_ip, port);
    if (ls == SOCK_BAD) { fprintf(stderr, "Cannot listen on %s:%d (is the port already in use?)\n", bind_ip, port); return 1; }
    sec = getenv("UNISPHERE_SECURE_COOKIE");
    printf("UNISPHERE running at http://%s:%d/  (press Ctrl+C to stop)\n", bind_ip, port);
    fflush(stdout);

    for (;;) {
        HttpReq req;
        sock_t cs = http_accept(ls, &req);
        int rc;
        if (cs == SOCK_BAD) continue;
        rc = http_read(cs, &req);
        if (rc == 0) {
            Ctx ctx;
            ctx.db = db; ctx.sock = cs; ctx.req = &req; ctx.secure_cookie = sec && *sec == '1';
            route(&ctx, root);
            printf("%s %s -> %d\n", req.method, req.path, http_last_status);   /* never logs bodies/passwords */
            fflush(stdout);
        } else if (rc > 0) {
            http_send(cs, rc, "text/plain", "Request rejected", 16, NULL, 1);
        }
        http_req_free(&req);
        http_close(cs);
    }
}
