#include "api.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int starts_with_nocase(const char *s, const char *prefix) {
    for (; *prefix; s++, prefix++) if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
    return 1;
}

void api_send(Ctx *c, int status, const char *json, const char *extra) {
    http_send(c->sock, status, "application/json; charset=utf-8", json, strlen(json), extra, 1);
}

void api_error(Ctx *c, int status, const char *code, const char *message) {
    Buf b; buf_init(&b);
    buf_puts(&b, "{\"ok\":false,\"error\":{\"code\":");
    buf_json_str(&b, code);
    buf_puts(&b, ",\"message\":");
    buf_json_str(&b, message);
    buf_puts(&b, "}}");
    api_send(c, status, b.d ? b.d : "{}", status == 429 ? "Retry-After: 900\r\n" : NULL);
    buf_free(&b);
}

/* If a browser sends Origin it must be this very site (blocks cross-site form/fetch attacks). */
int api_origin_ok(const HttpReq *r) {
    char expect[300];
    if (r->origin[0] == '\0') return 1;
    if (r->host[0] == '\0') return 0;
    snprintf(expect, sizeof expect, "http://%s", r->host);
    if (strcmp(r->origin, expect) == 0) return 1;
    snprintf(expect, sizeof expect, "https://%s", r->host);
    return strcmp(r->origin, expect) == 0;
}

int api_read_json(Ctx *c, JObj *o) {
    const HttpReq *r = c->req;
    if (!api_origin_ok(r)) { api_error(c, 403, "bad_origin", "Cross-site request blocked."); return 0; }
    if (!starts_with_nocase(r->content_type, "application/json")) {
        api_error(c, 415, "unsupported_media_type", "Content-Type must be application/json."); return 0;
    }
    if (r->body_len == 0 || json_parse_flat(r->body, o) != 0) {
        api_error(c, 400, "invalid_json", "Request body must be a flat JSON object."); return 0;
    }
    return 1;
}

/* Maps a failed SQLite statement to a clean HTTP error (never leaks SQL text). */
void api_db_error(Ctx *c) {
    const char *m = sqlite3_errmsg(c->db);
    if (strstr(m, "EVENT_FULL")) api_error(c, 409, "event_full", "This event is full.");
    else if (strstr(m, "UNIQUE")) api_error(c, 409, "duplicate", "That value already exists.");
    else if (strstr(m, "FOREIGN KEY")) api_error(c, 422, "invalid_reference", "A referenced club or event does not exist, or the record is still in use.");
    else if (strstr(m, "CHECK")) api_error(c, 422, "invalid_combination", "Values are inconsistent (end time must be after start time; registration dates in order).");
    else api_error(c, 500, "server_error", "Database error.");
}
