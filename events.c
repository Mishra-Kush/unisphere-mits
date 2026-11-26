#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fields.h"
#include "handlers.h"

#define EVENT_SELECT "SELECT e.id,e.club_id,c.name,c.slug,e.title,e.description,e.category,COALESCE(e.cover_image,''),e.event_date,e.start_time,e.end_time,e.venue,COALESCE(e.reg_opens,''),COALESCE(e.reg_closes,''),COALESCE(e.capacity,0),(SELECT count(*) FROM registrations r WHERE r.event_id=e.id),e.is_published,e.registration_enabled,e.status,e.created_at,e.updated_at,(SELECT count(*) FROM registrations r2 WHERE r2.event_id=e.id AND r2.user_id=?) FROM events e JOIN clubs c ON c.id=e.club_id WHERE 1=1"

static const char *const EV_CATS[] = {"workshop","hackathon","technical-fest","cultural-fest","seminar","competition","sports","music-dance","career","environmental","community-service","other",NULL};
static const char *const EV_STATUS[] = {"active","cancelled","archived",NULL};
#define NF 14
static const Field EV_FIELDS[NF] = {
    {"club_id","club_id",F_INT,1,1000000000,NULL,1,0,"Club"},
    {"title","title",F_TEXT,3,150,NULL,1,0,"Title"},
    {"description","description",F_TEXT,1,2000,NULL,1,0,"Description"},
    {"category","category",F_ENUM,0,0,EV_CATS,1,0,"Category"},
    {"cover_image","cover_image",F_IMAGE,0,200,NULL,0,1,"Cover image"},
    {"event_date","event_date",F_DATE,0,0,NULL,1,0,"Event date"},
    {"start_time","start_time",F_TIME,0,0,NULL,1,0,"Start time"},
    {"end_time","end_time",F_TIME,0,0,NULL,1,0,"End time"},
    {"venue","venue",F_TEXT,2,150,NULL,1,0,"Venue"},
    {"reg_opens","reg_opens",F_DATE,0,0,NULL,0,1,"Registration opening date"},
    {"reg_closes","reg_closes",F_DATE,0,0,NULL,0,1,"Registration closing date"},
    {"capacity","capacity",F_INT,1,100000,NULL,0,1,"Capacity"},
    {"is_published","is_published",F_BOOL,0,0,NULL,0,0,"Published flag"},
    {"registration_enabled","registration_enabled",F_BOOL,0,0,NULL,0,0,"Registration flag"},
};
/* status is handled separately (admin may cancel/archive via PUT) */
static const Field STATUS_FIELD = {"status","status",F_ENUM,0,0,EV_STATUS,0,0,"Status"};

const char *event_state(const char *status, int enabled, const char *date, const char *start, const char *end,
                        const char *ro, const char *rc, int cap, int count, const Now *now) {
    if (!strcmp(status, "cancelled")) return "cancelled";
    if (strcmp(date, now->today) < 0 || (!strcmp(date, now->today) && strcmp(end, now->hm) <= 0)) return "ended";
    if (!enabled) return "disabled";
    if (ro && *ro && strcmp(now->today, ro) < 0) return "not_open";
    if ((rc && *rc && strcmp(now->today, rc) > 0) || (!strcmp(date, now->today) && strcmp(start, now->hm) <= 0)) return "closed";
    if (cap > 0 && count >= cap) return "full";
    return "open";
}

static void emit_event(Buf *b, sqlite3_stmt *s, const Now *now) {
    int cap = sqlite3_column_int(s, 14), cnt = sqlite3_column_int(s, 15);
    buf_printf(b, "{\"id\":%d", sqlite3_column_int(s, 0));
    kv_i(b, "club_id", sqlite3_column_int(s, 1)); kv_s(b, "club_name", TXT(s, 2)); kv_s(b, "club_slug", TXT(s, 3));
    kv_s(b, "title", TXT(s, 4)); kv_s(b, "description", TXT(s, 5)); kv_s(b, "category", TXT(s, 6));
    kv_sn(b, "cover_image", TXT(s, 7)); kv_s(b, "event_date", TXT(s, 8)); kv_s(b, "start_time", TXT(s, 9));
    kv_s(b, "end_time", TXT(s, 10)); kv_s(b, "venue", TXT(s, 11)); kv_sn(b, "reg_opens", TXT(s, 12)); kv_sn(b, "reg_closes", TXT(s, 13));
    kv_in(b, "capacity", cap); kv_i(b, "registered_count", cnt);
    kv_b(b, "is_published", sqlite3_column_int(s, 16)); kv_b(b, "registration_enabled", sqlite3_column_int(s, 17));
    kv_s(b, "status", TXT(s, 18));
    kv_s(b, "registration_status", event_state(TXT(s, 18), sqlite3_column_int(s, 17), TXT(s, 8), TXT(s, 9), TXT(s, 10), TXT(s, 12), TXT(s, 13), cap, cnt, now));
    kv_s(b, "created_at", TXT(s, 19)); kv_s(b, "updated_at", TXT(s, 20));
    kv_b(b, "is_registered", sqlite3_column_int(s, 21) > 0);
    buf_puts(b, "}");
}

static int all_digits(const char *s) { if (!*s) return 0; for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0; return 1; }

int events_write_list(Ctx *c, Buf *b, const EvOpts *o, const Now *now) {
    Q q; char v[200], pat[420], uid[16], cid[16]; sqlite3_stmt *st; int n = 0, limit = o->limit_default, offset = 0;
    snprintf(uid, sizeof uid, "%d", o->uid);
    q_init(&q, EVENT_SELECT);
    q_param(&q, uid);
    if (o->mine) q_and(&q, "e.id IN (SELECT event_id FROM registrations WHERE user_id=?)", uid);
    else if (!o->admin) q_and(&q, "e.is_published=1 AND e.status!='archived'", NULL);
    if (o->club_id > 0) { snprintf(cid, sizeof cid, "%d", o->club_id); q_and(&q, "e.club_id=?", cid); }
    if (qs_get(o->query, "club", v, sizeof v) && all_digits(v) && strlen(v) < 10) q_and(&q, "e.club_id=?", v);
    if (qs_get(o->query, "category", v, sizeof v) && *v) q_and(&q, "c.category=?", v);          /* the organising club's category */
    if (qs_get(o->query, "event_category", v, sizeof v) && *v) q_and(&q, "e.category=?", v);
    if (qs_get(o->query, "from", v, sizeof v) && valid_date(v)) q_and(&q, "e.event_date>=?", v);
    if (qs_get(o->query, "to", v, sizeof v) && valid_date(v)) q_and(&q, "e.event_date<=?", v);
    if (qs_get(o->query, "upcoming", v, sizeof v) && !strcmp(v, "1")) q_and(&q, "e.event_date>=?", now->today);
    if (o->admin) {
        if (qs_get(o->query, "status", v, sizeof v) && in_list(v, EV_STATUS)) q_and(&q, "e.status=?", v);
        if (qs_get(o->query, "published", v, sizeof v) && (!strcmp(v, "0") || !strcmp(v, "1"))) q_and(&q, "e.is_published=?", v);
    }
    if (qs_get(o->query, "q", v, sizeof v) && *v) {
        like_pattern(v, pat, sizeof pat);
        q_like(&q, "(e.title LIKE ? ESCAPE '\\' OR e.description LIKE ? ESCAPE '\\' OR e.category LIKE ? ESCAPE '\\' OR e.venue LIKE ? ESCAPE '\\' OR c.name LIKE ? ESCAPE '\\' OR c.instagram_handle LIKE ? ESCAPE '\\' OR c.category LIKE ? ESCAPE '\\')", pat, 7);
    }
    if (qs_get(o->query, "limit", v, sizeof v) && atoi(v) > 0) limit = atoi(v) > 100 ? 100 : atoi(v);
    if (qs_get(o->query, "offset", v, sizeof v) && atoi(v) > 0) offset = atoi(v);
    if (qs_get(o->query, "sort", v, sizeof v) && !strcmp(v, "recent")) buf_puts(&q.sql, " ORDER BY e.created_at DESC, e.id DESC");
    else buf_puts(&q.sql, " ORDER BY e.event_date, e.start_time, e.id");
    buf_printf(&q.sql, " LIMIT %d OFFSET %d", limit, offset);
    st = q_prepare(&q, c->db);
    buf_puts(b, "[");
    if (st) while (sqlite3_step(st) == SQLITE_ROW) { if (n++) buf_puts(b, ","); emit_event(b, st, now); }
    buf_puts(b, "]");
    sqlite3_finalize(st);
    return n;
}

static int optional_student(Ctx *c) { Session s; return auth_current(c, &s) && !strcmp(s.role, "student") ? s.user_id : 0; }

void events_list(Ctx *c, int admin) {
    Session s; Buf b; Now now; EvOpts o; int n;
    if (admin && !auth_require(c, &s, "admin")) return;
    ist_now(&now);
    memset(&o, 0, sizeof o); o.query = c->req->query; o.admin = admin; o.uid = admin ? 0 : optional_student(c); o.limit_default = 50;
    buf_init(&b);
    buf_puts(&b, "{\"ok\":true,\"events\":");
    n = events_write_list(c, &b, &o, &now);
    buf_printf(&b, ",\"count\":%d}", n);
    api_send(c, 200, b.d, NULL);
    buf_free(&b);
}

static void send_event(Ctx *c, int id, int status) {
    sqlite3_stmt *st = NULL; Buf b; Now now; Session s; int admin, uid = 0;
    admin = auth_current(c, &s) && !strcmp(s.role, "admin");
    if (!admin && auth_current(c, &s) && !strcmp(s.role, "student")) uid = s.user_id;
    ist_now(&now);
    if (sqlite3_prepare_v2(c->db, EVENT_SELECT " AND e.id=?", -1, &st, NULL) == SQLITE_OK) { sqlite3_bind_int(st, 1, uid); sqlite3_bind_int(st, 2, id); }
    buf_init(&b);
    if (st && sqlite3_step(st) == SQLITE_ROW && (admin || (sqlite3_column_int(st, 16) && strcmp(TXT(st, 18), "archived")))) {
        buf_puts(&b, "{\"ok\":true,\"event\":"); emit_event(&b, st, &now); buf_puts(&b, "}");
        api_send(c, status, b.d, NULL);
    } else api_error(c, 404, "not_found", "Event not found.");
    sqlite3_finalize(st); buf_free(&b);
}

void events_get(Ctx *c, int id) { send_event(c, id, 200); }

void search_all(Ctx *c) {
    Buf b; Now now; EvOpts o; int nc, ne; char v[200] = "";
    ist_now(&now);
    memset(&o, 0, sizeof o); o.query = c->req->query; o.uid = optional_student(c); o.limit_default = 8;
    buf_init(&b);
    qs_get(c->req->query, "q", v, sizeof v);
    buf_puts(&b, "{\"ok\":true,\"query\":"); buf_json_str(&b, v);
    buf_puts(&b, ",\"clubs\":"); nc = clubs_write_list(c, &b, c->req->query, 0, 8);
    buf_puts(&b, ",\"events\":"); ne = events_write_list(c, &b, &o, &now);
    buf_printf(&b, ",\"club_count\":%d,\"event_count\":%d}", nc, ne);
    api_send(c, 200, b.d, NULL);
    buf_free(&b);
}

static int cross_check(Ctx *c, char (*out)[FIELD_MAX], const int *present, const int *isnull) {
    const char *st = fields_val(EV_FIELDS, NF, out, present, isnull, "start_time"), *en = fields_val(EV_FIELDS, NF, out, present, isnull, "end_time");
    const char *ro = fields_val(EV_FIELDS, NF, out, present, isnull, "reg_opens"), *rc = fields_val(EV_FIELDS, NF, out, present, isnull, "reg_closes");
    if (st && en && strcmp(en, st) <= 0) { api_error(c, 400, "invalid_field", "End time must be after start time."); return 0; }
    if (ro && rc && strcmp(ro, rc) > 0) { api_error(c, 400, "invalid_field", "Registration opening date must not be after the closing date."); return 0; }
    return 1;
}

void events_create(Ctx *c) {
    Session s; JObj o; static char out[MAX_FIELDS][FIELD_MAX]; int present[MAX_FIELDS], isnull[MAX_FIELDS], id; char t[40];
    Field fs[NF + 1];
    if (!auth_require(c, &s, "admin") || !api_read_json(c, &o)) return;
    if (!fields_validate(c, &o, EV_FIELDS, NF, 1, out, present, isnull)) { json_free(&o); return; }
    json_free(&o);
    if (!cross_check(c, out, present, isnull)) return;
    memcpy(fs, EV_FIELDS, sizeof EV_FIELDS); fs[NF] = STATUS_FIELD; present[NF] = 0; isnull[NF] = 0;
    if (fields_insert(c->db, "events", fs, NF + 1, out, present, isnull) != 0) { api_db_error(c); return; }
    id = (int)sqlite3_last_insert_rowid(c->db);
    snprintf(t, sizeof t, "event:%d", id);
    audit_log(c->db, s.user_id, "event_created", t);
    send_event(c, id, 201);
}

void events_update(Ctx *c, int id) {
    Session s; JObj o; static char out[MAX_FIELDS][FIELD_MAX]; int present[MAX_FIELDS], isnull[MAX_FIELDS], i, any = 0; char t[40];
    Field fs[NF + 1];
    if (!auth_require(c, &s, "admin") || !api_read_json(c, &o)) return;
    memcpy(fs, EV_FIELDS, sizeof EV_FIELDS); fs[NF] = STATUS_FIELD;
    if (!fields_validate(c, &o, fs, NF + 1, 0, out, present, isnull)) { json_free(&o); return; }
    json_free(&o);
    for (i = 0; i <= NF; i++) any |= present[i];
    if (!any) { api_error(c, 400, "no_fields", "No valid fields to update."); return; }
    if (!cross_check(c, out, present, isnull)) return;
    { const char *cap = fields_val(fs, NF + 1, out, present, isnull, "capacity");
      if (cap) {
          sqlite3_stmt *st = NULL; int cnt = 0;
          if (sqlite3_prepare_v2(c->db, "SELECT count(*) FROM registrations WHERE event_id=?1", -1, &st, NULL) == SQLITE_OK) { sqlite3_bind_int(st, 1, id); if (sqlite3_step(st) == SQLITE_ROW) cnt = sqlite3_column_int(st, 0); }
          sqlite3_finalize(st);
          if (atoi(cap) < cnt) { api_error(c, 422, "capacity_too_low", "Capacity cannot be lower than the number of existing registrations."); return; }
      } }
    if (fields_update(c->db, "events", id, fs, NF + 1, out, present, isnull) != 0) { api_db_error(c); return; }
    if (sqlite3_changes(c->db) == 0) { api_error(c, 404, "not_found", "Event not found."); return; }
    snprintf(t, sizeof t, "event:%d", id);
    audit_log(c->db, s.user_id, "event_updated", t);
    send_event(c, id, 200);
}

/* "Delete" archives the event: it disappears from the public site but registrations are kept. */
void events_archive(Ctx *c, int id) {
    Session s; sqlite3_stmt *st = NULL; char t[40];
    if (!auth_require(c, &s, "admin")) return;
    if (sqlite3_prepare_v2(c->db, "UPDATE events SET status='archived',is_published=0,updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?1", -1, &st, NULL) == SQLITE_OK) { sqlite3_bind_int(st, 1, id); sqlite3_step(st); }
    sqlite3_finalize(st);
    if (sqlite3_changes(c->db) == 0) { api_error(c, 404, "not_found", "Event not found."); return; }
    snprintf(t, sizeof t, "event:%d", id);
    audit_log(c->db, s.user_id, "event_archived", t);
    api_send(c, 200, "{\"ok\":true,\"archived\":true}", NULL);
}
