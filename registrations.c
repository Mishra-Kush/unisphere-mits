/* One-click registration: validated in a transaction so duplicates and overbooking are impossible. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "handlers.h"

static void rollback_error(Ctx *c, int status, const char *code, const char *msg) {
    sqlite3_exec(c->db, "ROLLBACK;", NULL, NULL, NULL);
    api_error(c, status, code, msg);
}

static int state_error(const char *state, const char **code, const char **msg) {
    if (!strcmp(state, "open")) return 0;
    if (!strcmp(state, "cancelled")) { *code = "event_cancelled"; *msg = "This event has been cancelled."; }
    else if (!strcmp(state, "ended")) { *code = "event_ended"; *msg = "This event has already ended."; }
    else if (!strcmp(state, "disabled")) { *code = "registration_disabled"; *msg = "Registration is not enabled for this event."; }
    else if (!strcmp(state, "not_open")) { *code = "registration_not_open"; *msg = "Registration has not opened yet."; }
    else if (!strcmp(state, "closed")) { *code = "registration_closed"; *msg = "Registration is closed."; }
    else { *code = "event_full"; *msg = "This event is full."; }
    return 1;
}

void reg_register(Ctx *c, int event_id) {
    Session s; Now now; sqlite3_stmt *st = NULL; int count = 0, cap, enabled, published, rc;
    char status[16], date[11], start[6], end[6], ro[11], rcl[11];
    const char *code = "", *msg = "";
    if (!auth_require(c, &s, "student")) return;
    ist_now(&now);
    if (sqlite3_exec(c->db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) { api_error(c, 500, "server_error", "Database busy."); return; }
    if (sqlite3_prepare_v2(c->db, "SELECT status,is_published,registration_enabled,event_date,start_time,end_time,COALESCE(reg_opens,''),COALESCE(reg_closes,''),COALESCE(capacity,0),(SELECT count(*) FROM registrations WHERE event_id=events.id) FROM events WHERE id=?1", -1, &st, NULL) != SQLITE_OK) { rollback_error(c, 500, "server_error", "Database error."); return; }
    sqlite3_bind_int(st, 1, event_id);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); rollback_error(c, 404, "not_found", "Event not found."); return; }
    snprintf(status, sizeof status, "%s", TXT(st, 0)); published = sqlite3_column_int(st, 1); enabled = sqlite3_column_int(st, 2);
    snprintf(date, sizeof date, "%s", TXT(st, 3)); snprintf(start, sizeof start, "%s", TXT(st, 4)); snprintf(end, sizeof end, "%s", TXT(st, 5));
    snprintf(ro, sizeof ro, "%s", TXT(st, 6)); snprintf(rcl, sizeof rcl, "%s", TXT(st, 7)); cap = sqlite3_column_int(st, 8); count = sqlite3_column_int(st, 9);
    sqlite3_finalize(st);
    if (!published || !strcmp(status, "archived")) { rollback_error(c, 404, "not_found", "Event not found."); return; }

    if (sqlite3_prepare_v2(c->db, "SELECT 1 FROM registrations WHERE user_id=?1 AND event_id=?2", -1, &st, NULL) == SQLITE_OK) { sqlite3_bind_int(st, 1, s.user_id); sqlite3_bind_int(st, 2, event_id); }
    rc = st && sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    if (rc) { rollback_error(c, 409, "already_registered", "You are already registered for this event."); return; }
    if (state_error(event_state(status, enabled, date, start, end, ro, rcl, cap, count, &now), &code, &msg)) { rollback_error(c, 409, code, msg); return; }

    st = NULL;
    if (sqlite3_prepare_v2(c->db, "INSERT INTO registrations(user_id,event_id) VALUES(?1,?2)", -1, &st, NULL) != SQLITE_OK) { rollback_error(c, 500, "server_error", "Database error."); return; }
    sqlite3_bind_int(st, 1, s.user_id); sqlite3_bind_int(st, 2, event_id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        const char *m = sqlite3_errmsg(c->db);
        if (strstr(m, "EVENT_FULL")) rollback_error(c, 409, "event_full", "This event is full.");
        else if (strstr(m, "UNIQUE")) rollback_error(c, 409, "already_registered", "You are already registered for this event.");
        else rollback_error(c, 500, "server_error", "Could not save registration.");
        return;
    }
    sqlite3_exec(c->db, "COMMIT;", NULL, NULL, NULL);
    { Buf b; buf_init(&b);
      buf_printf(&b, "{\"ok\":true,\"registration\":{\"event_id\":%d,\"status\":\"registered\",\"registered_count\":%d}}", event_id, count + 1);
      api_send(c, 201, b.d, NULL); buf_free(&b); }
}

void reg_cancel(Ctx *c, int event_id) {
    Session s; Now now; sqlite3_stmt *st = NULL; char date[11] = "", start[6] = ""; int found = 0;
    if (!auth_require(c, &s, "student")) return;
    ist_now(&now);
    if (sqlite3_prepare_v2(c->db, "SELECT event_date,start_time FROM events WHERE id=?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, event_id);
        if (sqlite3_step(st) == SQLITE_ROW) { found = 1; snprintf(date, sizeof date, "%s", TXT(st, 0)); snprintf(start, sizeof start, "%s", TXT(st, 1)); }
    }
    sqlite3_finalize(st);
    if (!found) { api_error(c, 404, "not_found", "Event not found."); return; }
    if (strcmp(date, now.today) < 0 || (!strcmp(date, now.today) && strcmp(start, now.hm) <= 0)) {
        api_error(c, 409, "cancellation_closed", "Registrations cannot be cancelled after the event has started."); return;
    }
    st = NULL;
    if (sqlite3_prepare_v2(c->db, "DELETE FROM registrations WHERE user_id=?1 AND event_id=?2", -1, &st, NULL) == SQLITE_OK) { sqlite3_bind_int(st, 1, s.user_id); sqlite3_bind_int(st, 2, event_id); sqlite3_step(st); }
    sqlite3_finalize(st);
    if (sqlite3_changes(c->db) == 0) { api_error(c, 404, "not_registered", "You are not registered for this event."); return; }
    api_send(c, 200, "{\"ok\":true,\"cancelled\":true}", NULL);
}

void reg_mine(Ctx *c) {
    Session s; Buf b; Now now; EvOpts o; int n;
    if (!auth_require(c, &s, "student")) return;
    ist_now(&now);
    memset(&o, 0, sizeof o); o.query = c->req->query; o.uid = s.user_id; o.mine = 1; o.limit_default = 100;
    buf_init(&b);
    buf_puts(&b, "{\"ok\":true,\"events\":");
    n = events_write_list(c, &b, &o, &now);
    buf_printf(&b, ",\"count\":%d}", n);
    api_send(c, 200, b.d, NULL);
    buf_free(&b);
}

/* CSV cells starting with = + - @ are prefixed with ' so spreadsheets never run them as formulas. */
static void csv_cell(Buf *b, const char *s) {
    buf_puts(b, "\"");
    if (*s == '=' || *s == '+' || *s == '-' || *s == '@') buf_puts(b, "'");
    for (; *s; s++) { if (*s == '"') buf_puts(b, "\"\""); else { char t[2] = {*s, 0}; buf_puts(b, t); } }
    buf_puts(b, "\"");
}

void reg_attendees(Ctx *c, int event_id) {
    Session s; sqlite3_stmt *st = NULL; Buf b; char fmt[16] = "", title[160] = "", hdr[160]; int n = 0, found = 0;
    if (!auth_require(c, &s, "admin")) return;
    if (sqlite3_prepare_v2(c->db, "SELECT title FROM events WHERE id=?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, event_id);
        if (sqlite3_step(st) == SQLITE_ROW) { found = 1; snprintf(title, sizeof title, "%s", TXT(st, 0)); }
    }
    sqlite3_finalize(st); st = NULL;
    if (!found) { api_error(c, 404, "not_found", "Event not found."); return; }
    qs_get(c->req->query, "format", fmt, sizeof fmt);
    if (sqlite3_prepare_v2(c->db, "SELECT u.id,u.full_name,u.email,COALESCE(u.enrollment_no,''),r.created_at FROM registrations r JOIN users u ON u.id=r.user_id WHERE r.event_id=?1 ORDER BY r.created_at,r.id", -1, &st, NULL) != SQLITE_OK) { api_error(c, 500, "server_error", "Database error."); return; }
    sqlite3_bind_int(st, 1, event_id);
    buf_init(&b);
    if (!strcmp(fmt, "csv")) {
        buf_puts(&b, "Name,Email,Enrollment No,Registered At (UTC)\r\n");
        while (sqlite3_step(st) == SQLITE_ROW) {
            csv_cell(&b, TXT(st, 1)); buf_puts(&b, ","); csv_cell(&b, TXT(st, 2)); buf_puts(&b, ","); csv_cell(&b, TXT(st, 3)); buf_puts(&b, ","); csv_cell(&b, TXT(st, 4)); buf_puts(&b, "\r\n");
        }
        snprintf(hdr, sizeof hdr, "Content-Disposition: attachment; filename=\"event-%d-registrations.csv\"\r\n", event_id);
        http_send(c->sock, 200, "text/csv; charset=utf-8", b.d, b.n, hdr, 1);
        { char t[40]; snprintf(t, sizeof t, "event:%d", event_id); audit_log(c->db, s.user_id, "attendees_exported", t); }
    } else {
        buf_puts(&b, "{\"ok\":true,\"event\":"); buf_json_str(&b, title); buf_puts(&b, ",\"attendees\":[");
        while (sqlite3_step(st) == SQLITE_ROW) {
            if (n++) buf_puts(&b, ",");
            buf_printf(&b, "{\"user_id\":%d", sqlite3_column_int(st, 0));
            kv_s(&b, "full_name", TXT(st, 1)); kv_s(&b, "email", TXT(st, 2)); kv_sn(&b, "enrollment_no", TXT(st, 3)); kv_s(&b, "registered_at", TXT(st, 4));
            buf_puts(&b, "}");
        }
        buf_printf(&b, "],\"count\":%d}", n);
        api_send(c, 200, b.d, NULL);
    }
    sqlite3_finalize(st); buf_free(&b);
}
