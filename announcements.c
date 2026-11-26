#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fields.h"
#include "handlers.h"

static const char *const PRIO[] = {"normal","important","urgent",NULL};
#define NF 7
static const Field ANN_FIELDS[NF] = {
    {"title","title",F_TEXT,3,150,NULL,1,0,"Title"},
    {"message","message",F_TEXT,1,2000,NULL,1,0,"Message"},
    {"priority","priority",F_ENUM,0,0,PRIO,0,0,"Priority"},
    {"club_id","club_id",F_INT,1,1000000000,NULL,0,1,"Related club"},
    {"event_id","event_id",F_INT,1,1000000000,NULL,0,1,"Related event"},
    {"is_published","is_published",F_BOOL,0,0,NULL,0,0,"Published flag"},
    {"expires_at","expires_at",F_DATE,0,0,NULL,0,1,"Expiry date"},
};
#define ANN_SELECT "SELECT a.id,a.title,a.message,a.priority,COALESCE(a.club_id,0),COALESCE(c.name,''),COALESCE(a.event_id,0),COALESCE(e.title,''),a.is_published,COALESCE(a.published_at,''),COALESCE(a.expires_at,''),a.created_at,a.updated_at FROM announcements a LEFT JOIN clubs c ON c.id=a.club_id LEFT JOIN events e ON e.id=a.event_id WHERE 1=1"

static void emit_ann(Buf *b, sqlite3_stmt *s, const Now *now) {
    const char *exp = TXT(s, 10);
    buf_printf(b, "{\"id\":%d", sqlite3_column_int(s, 0));
    kv_s(b, "title", TXT(s, 1)); kv_s(b, "message", TXT(s, 2)); kv_s(b, "priority", TXT(s, 3));
    kv_in(b, "club_id", sqlite3_column_int(s, 4)); kv_sn(b, "club_name", TXT(s, 5));
    kv_in(b, "event_id", sqlite3_column_int(s, 6)); kv_sn(b, "event_title", TXT(s, 7));
    kv_b(b, "is_published", sqlite3_column_int(s, 8)); kv_sn(b, "published_at", TXT(s, 9)); kv_sn(b, "expires_at", exp);
    kv_b(b, "is_expired", *exp && strcmp(exp, now->today) < 0);
    kv_s(b, "created_at", TXT(s, 11)); kv_s(b, "updated_at", TXT(s, 12));
    buf_puts(b, "}");
}

void ann_list(Ctx *c, int admin) {
    Session s; Q q; Buf b; Now now; sqlite3_stmt *st; char v[100]; int n = 0, limit = 50;
    if (admin && !auth_require(c, &s, "admin")) return;
    ist_now(&now);
    q_init(&q, ANN_SELECT);
    if (!admin) q_and(&q, "a.is_published=1 AND (a.expires_at IS NULL OR a.expires_at>=?)", now.today);
    if (qs_get(c->req->query, "priority", v, sizeof v) && in_list(v, PRIO)) q_and(&q, "a.priority=?", v);
    if (qs_get(c->req->query, "club", v, sizeof v) && atoi(v) > 0) { snprintf(v, sizeof v, "%d", atoi(v)); q_and(&q, "a.club_id=?", v); }
    if (qs_get(c->req->query, "limit", v, sizeof v) && atoi(v) > 0) limit = atoi(v) > 100 ? 100 : atoi(v);
    buf_puts(&q.sql, admin ? " ORDER BY a.created_at DESC, a.id DESC" : " ORDER BY CASE a.priority WHEN 'urgent' THEN 0 WHEN 'important' THEN 1 ELSE 2 END, a.published_at DESC, a.id DESC");
    buf_printf(&q.sql, " LIMIT %d", limit);
    st = q_prepare(&q, c->db);
    buf_init(&b);
    buf_puts(&b, "{\"ok\":true,\"announcements\":[");
    if (st) while (sqlite3_step(st) == SQLITE_ROW) { if (n++) buf_puts(&b, ","); emit_ann(&b, st, &now); }
    buf_printf(&b, "],\"count\":%d}", n);
    sqlite3_finalize(st);
    api_send(c, 200, b.d, NULL);
    buf_free(&b);
}

static void send_ann(Ctx *c, int id, int status) {
    sqlite3_stmt *st = NULL; Buf b; Now now; ist_now(&now);
    if (sqlite3_prepare_v2(c->db, ANN_SELECT " AND a.id=?1", -1, &st, NULL) == SQLITE_OK) sqlite3_bind_int(st, 1, id);
    buf_init(&b);
    if (st && sqlite3_step(st) == SQLITE_ROW) { buf_puts(&b, "{\"ok\":true,\"announcement\":"); emit_ann(&b, st, &now); buf_puts(&b, "}"); api_send(c, status, b.d, NULL); }
    else api_error(c, 404, "not_found", "Announcement not found.");
    sqlite3_finalize(st); buf_free(&b);
}

static void stamp_published(Ctx *c, int id) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(c->db, "UPDATE announcements SET published_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?1 AND is_published=1 AND published_at IS NULL", -1, &st, NULL) == SQLITE_OK) { sqlite3_bind_int(st, 1, id); sqlite3_step(st); }
    sqlite3_finalize(st);
}

void ann_create(Ctx *c) {
    Session s; JObj o; static char out[MAX_FIELDS][FIELD_MAX]; int present[MAX_FIELDS], isnull[MAX_FIELDS], id; char t[40];
    if (!auth_require(c, &s, "admin") || !api_read_json(c, &o)) return;
    if (!fields_validate(c, &o, ANN_FIELDS, NF, 1, out, present, isnull)) { json_free(&o); return; }
    json_free(&o);
    if (fields_insert(c->db, "announcements", ANN_FIELDS, NF, out, present, isnull) != 0) { api_db_error(c); return; }
    id = (int)sqlite3_last_insert_rowid(c->db);
    stamp_published(c, id);
    snprintf(t, sizeof t, "announcement:%d", id);
    audit_log(c->db, s.user_id, "announcement_created", t);
    send_ann(c, id, 201);
}

void ann_update(Ctx *c, int id) {
    Session s; JObj o; static char out[MAX_FIELDS][FIELD_MAX]; int present[MAX_FIELDS], isnull[MAX_FIELDS], i, any = 0; char t[40];
    if (!auth_require(c, &s, "admin") || !api_read_json(c, &o)) return;
    if (!fields_validate(c, &o, ANN_FIELDS, NF, 0, out, present, isnull)) { json_free(&o); return; }
    json_free(&o);
    for (i = 0; i < NF; i++) any |= present[i];
    if (!any) { api_error(c, 400, "no_fields", "No valid fields to update."); return; }
    if (fields_update(c->db, "announcements", id, ANN_FIELDS, NF, out, present, isnull) != 0) { api_db_error(c); return; }
    if (sqlite3_changes(c->db) == 0) { api_error(c, 404, "not_found", "Announcement not found."); return; }
    stamp_published(c, id);
    snprintf(t, sizeof t, "announcement:%d", id);
    audit_log(c->db, s.user_id, "announcement_updated", t);
    send_ann(c, id, 200);
}

void ann_delete(Ctx *c, int id) {
    Session s; sqlite3_stmt *st = NULL; char t[40];
    if (!auth_require(c, &s, "admin")) return;
    if (sqlite3_prepare_v2(c->db, "DELETE FROM announcements WHERE id=?1", -1, &st, NULL) == SQLITE_OK) { sqlite3_bind_int(st, 1, id); sqlite3_step(st); }
    sqlite3_finalize(st);
    if (sqlite3_changes(c->db) == 0) { api_error(c, 404, "not_found", "Announcement not found."); return; }
    snprintf(t, sizeof t, "announcement:%d", id);
    audit_log(c->db, s.user_id, "announcement_deleted", t);
    api_send(c, 200, "{\"ok\":true,\"deleted\":true}", NULL);
}

static int scalar(Ctx *c, const char *sql, const char *p1) {
    sqlite3_stmt *st = NULL; int v = 0;
    if (sqlite3_prepare_v2(c->db, sql, -1, &st, NULL) == SQLITE_OK) {
        if (p1) sqlite3_bind_text(st, 1, p1, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return v;
}

void admin_dashboard(Ctx *c) {
    Session s; Buf b; Now now; sqlite3_stmt *st = NULL; int n = 0;
    if (!auth_require(c, &s, "admin")) return;
    ist_now(&now);
    buf_init(&b);
    buf_puts(&b, "{\"ok\":true,\"stats\":{");
    buf_printf(&b, "\"clubs\":%d", scalar(c, "SELECT count(*) FROM clubs WHERE is_archived=0", NULL));
    kv_i(&b, "published_events", scalar(c, "SELECT count(*) FROM events WHERE is_published=1 AND status='active'", NULL));
    kv_i(&b, "upcoming_events", scalar(c, "SELECT count(*) FROM events WHERE is_published=1 AND status='active' AND event_date>=?1", now.today));
    kv_i(&b, "registrations", scalar(c, "SELECT count(*) FROM registrations", NULL));
    kv_i(&b, "students", scalar(c, "SELECT count(*) FROM users WHERE role='student'", NULL));
    kv_i(&b, "active_announcements", scalar(c, "SELECT count(*) FROM announcements WHERE is_published=1 AND (expires_at IS NULL OR expires_at>=?1)", now.today));
    buf_puts(&b, "},\"recent_announcements\":[");
    if (sqlite3_prepare_v2(c->db, "SELECT id,title,priority,is_published,created_at FROM announcements ORDER BY created_at DESC,id DESC LIMIT 5", -1, &st, NULL) == SQLITE_OK)
        while (sqlite3_step(st) == SQLITE_ROW) {
            if (n++) buf_puts(&b, ",");
            buf_printf(&b, "{\"id\":%d", sqlite3_column_int(st, 0)); kv_s(&b, "title", TXT(st, 1)); kv_s(&b, "priority", TXT(st, 2));
            kv_b(&b, "is_published", sqlite3_column_int(st, 3)); kv_s(&b, "created_at", TXT(st, 4)); buf_puts(&b, "}");
        }
    sqlite3_finalize(st); st = NULL; n = 0;
    buf_puts(&b, "],\"recent_activity\":[");
    if (sqlite3_prepare_v2(c->db, "SELECT l.action,COALESCE(l.target,''),l.created_at,COALESCE(u.email,'') FROM audit_logs l LEFT JOIN users u ON u.id=l.user_id ORDER BY l.id DESC LIMIT 10", -1, &st, NULL) == SQLITE_OK)
        while (sqlite3_step(st) == SQLITE_ROW) {
            if (n++) buf_puts(&b, ",");
            buf_puts(&b, "{\"action\":"); buf_json_str(&b, TXT(st, 0)); kv_s(&b, "target", TXT(st, 1)); kv_s(&b, "created_at", TXT(st, 2)); kv_s(&b, "by", TXT(st, 3)); buf_puts(&b, "}");
        }
    sqlite3_finalize(st);
    buf_puts(&b, "]}");
    api_send(c, 200, b.d, NULL);
    buf_free(&b);
}
