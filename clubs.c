#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fields.h"
#include "handlers.h"
#include "password.h"

#define CLUB_COLS "c.id,c.name,c.slug,c.description,COALESCE(c.instagram_handle,''),c.category,COALESCE(c.image_path,''),c.faculty_advisor,c.student_leads,c.recruitment_status,c.is_archived,c.created_at,c.updated_at"

static const char *const CLUB_CATS[] = {"technical","coding-ai","cultural","music-dance","sports-fitness","arts-photography","entrepreneurship-career","environment-social","research-innovation","wellness-community",NULL};
static const char *const RECRUIT[] = {"open","closed","To be announced",NULL};
#define NF 8
static const Field CLUB_FIELDS[NF] = {
    {"name","name",F_TEXT,2,100,NULL,1,0,"Club name"},
    {"description","description",F_TEXT,1,500,NULL,1,0,"Description"},
    {"instagram_handle","instagram_handle",F_HANDLE,0,0,NULL,0,1,"Instagram handle"},
    {"category","category",F_ENUM,0,0,CLUB_CATS,1,0,"Category"},
    {"image_path","image_path",F_IMAGE,0,200,NULL,0,1,"Image path"},
    {"faculty_advisor","faculty_advisor",F_TEXT,0,200,NULL,0,0,"Faculty advisor"},
    {"student_leads","student_leads",F_TEXT,0,200,NULL,0,0,"Student leads"},
    {"recruitment_status","recruitment_status",F_ENUM,0,0,RECRUIT,0,0,"Recruitment status"},
};

static void emit_club(Buf *b, sqlite3_stmt *s) {
    char url[160];
    instagram_url(TXT(s, 4), url, sizeof url);
    buf_printf(b, "{\"id\":%d", sqlite3_column_int(s, 0));
    kv_s(b, "name", TXT(s, 1)); kv_s(b, "slug", TXT(s, 2)); kv_s(b, "description", TXT(s, 3));
    kv_sn(b, "instagram_handle", TXT(s, 4)); kv_sn(b, "instagram_url", url);
    kv_s(b, "category", TXT(s, 5)); kv_sn(b, "image_path", TXT(s, 6));
    kv_s(b, "faculty_advisor", TXT(s, 7)); kv_s(b, "student_leads", TXT(s, 8));
    kv_s(b, "recruitment_status", TXT(s, 9)); kv_b(b, "is_archived", sqlite3_column_int(s, 10));
    kv_s(b, "created_at", TXT(s, 11)); kv_s(b, "updated_at", TXT(s, 12));
    buf_puts(b, "}");
}

int clubs_write_list(Ctx *c, Buf *b, const char *query, int admin, int limit_default) {
    Q q; char v[200], pat[420]; sqlite3_stmt *st; int n = 0, limit = limit_default, offset = 0;
    q_init(&q, "SELECT " CLUB_COLS " FROM clubs c WHERE 1=1");
    if (!(admin && qs_get(query, "all", v, sizeof v) && !strcmp(v, "1"))) q_and(&q, "c.is_archived=0", NULL);
    if (qs_get(query, "category", v, sizeof v) && *v) q_and(&q, "c.category=?", v);
    if (qs_get(query, "recruitment", v, sizeof v) && *v) q_and(&q, "c.recruitment_status=?", v);
    if (qs_get(query, "q", v, sizeof v) && *v) {
        like_pattern(v, pat, sizeof pat);
        q_like(&q, "(c.name LIKE ? ESCAPE '\\' OR c.instagram_handle LIKE ? ESCAPE '\\' OR c.description LIKE ? ESCAPE '\\' OR c.category LIKE ? ESCAPE '\\')", pat, 4);
    }
    if (qs_get(query, "limit", v, sizeof v) && atoi(v) > 0) limit = atoi(v) > 200 ? 200 : atoi(v);
    if (qs_get(query, "offset", v, sizeof v) && atoi(v) > 0) offset = atoi(v);
    buf_puts(&q.sql, " ORDER BY c.name COLLATE NOCASE, c.id");
    buf_printf(&q.sql, " LIMIT %d OFFSET %d", limit, offset);
    st = q_prepare(&q, c->db);
    buf_puts(b, "[");
    if (st) while (sqlite3_step(st) == SQLITE_ROW) { if (n++) buf_puts(b, ","); emit_club(b, st); }
    buf_puts(b, "]");
    sqlite3_finalize(st);
    return n;
}

void clubs_list(Ctx *c, int admin) {
    Session s; Buf b; int n;
    if (admin && !auth_require(c, &s, "admin")) return;
    buf_init(&b);
    buf_puts(&b, "{\"ok\":true,\"clubs\":");
    n = clubs_write_list(c, &b, c->req->query, admin, 100);
    buf_printf(&b, ",\"count\":%d}", n);
    api_send(c, 200, b.d, NULL);
    buf_free(&b);
}

static void send_club(Ctx *c, int id, int status, int admin) {
    sqlite3_stmt *st = NULL; Buf b; buf_init(&b);
    if (sqlite3_prepare_v2(c->db, "SELECT " CLUB_COLS " FROM clubs c WHERE c.id=?1", -1, &st, NULL) == SQLITE_OK) sqlite3_bind_int(st, 1, id);
    if (st && sqlite3_step(st) == SQLITE_ROW && (admin || !sqlite3_column_int(st, 10))) {
        buf_puts(&b, "{\"ok\":true,\"club\":"); emit_club(&b, st); buf_puts(&b, "}");
        api_send(c, status, b.d, NULL);
    } else api_error(c, 404, "not_found", "Club not found.");
    sqlite3_finalize(st); buf_free(&b);
}

void clubs_get(Ctx *c, int id) { Session s; send_club(c, id, 200, auth_current(c, &s) && !strcmp(s.role, "admin")); }

void clubs_events(Ctx *c, int id) {
    Session s; Buf b; Now now; EvOpts o; int n, uid = 0;
    sqlite3_stmt *st = NULL; int exists = 0;
    if (sqlite3_prepare_v2(c->db, "SELECT 1 FROM clubs WHERE id=?1 AND is_archived=0", -1, &st, NULL) == SQLITE_OK) { sqlite3_bind_int(st, 1, id); exists = sqlite3_step(st) == SQLITE_ROW; }
    sqlite3_finalize(st);
    if (!exists) { api_error(c, 404, "not_found", "Club not found."); return; }
    if (auth_current(c, &s) && !strcmp(s.role, "student")) uid = s.user_id;
    ist_now(&now);
    memset(&o, 0, sizeof o); o.query = c->req->query; o.club_id = id; o.uid = uid; o.limit_default = 50;
    buf_init(&b);
    buf_puts(&b, "{\"ok\":true,\"events\":");
    n = events_write_list(c, &b, &o, &now);
    buf_printf(&b, ",\"count\":%d}", n);
    api_send(c, 200, b.d, NULL);
    buf_free(&b);
}

static void defaults(const int *present, char (*out)[FIELD_MAX]) {
    int i;
    for (i = 0; i < NF; i++)
        if (present[i] && out[i][0] == '\0' && (!strcmp(CLUB_FIELDS[i].name, "faculty_advisor") || !strcmp(CLUB_FIELDS[i].name, "student_leads")))
            strcpy(out[i], "Not yet provided");
}

void clubs_create(Ctx *c) {
    Session s; JObj o; static char out[MAX_FIELDS][FIELD_MAX]; int present[MAX_FIELDS], isnull[MAX_FIELDS], id;
    Field fs[NF + 1]; uint8_t rb[8]; char slug[100], base[60]; int i;
    if (!auth_require(c, &s, "admin") || !api_read_json(c, &o)) return;
    if (!fields_validate(c, &o, CLUB_FIELDS, NF, 1, out, present, isnull)) { json_free(&o); return; }
    json_free(&o);
    defaults(present, out);
    memcpy(fs, CLUB_FIELDS, sizeof CLUB_FIELDS);
    fs[NF] = (Field){"slug","slug",F_TEXT,0,100,NULL,0,0,"Slug"};
    random_bytes(rb, sizeof rb);
    strcpy(out[NF], "tmp-"); for (i = 0; i < 8; i++) sprintf(out[NF] + 4 + 2 * i, "%02x", rb[i]);
    present[NF] = 1; isnull[NF] = 0;
    if (fields_insert(c->db, "clubs", fs, NF + 1, out, present, isnull) != 0) { api_db_error(c); return; }
    id = (int)sqlite3_last_insert_rowid(c->db);
    slugify(out[0], base, sizeof base);
    snprintf(slug, sizeof slug, "club-%d-%s", id, base);
    { sqlite3_stmt *st = NULL;
      if (sqlite3_prepare_v2(c->db, "UPDATE clubs SET slug=?1 WHERE id=?2", -1, &st, NULL) == SQLITE_OK) { sqlite3_bind_text(st, 1, slug, -1, SQLITE_TRANSIENT); sqlite3_bind_int(st, 2, id); sqlite3_step(st); }
      sqlite3_finalize(st); }
    snprintf(base, sizeof base, "club:%d", id);
    audit_log(c->db, s.user_id, "club_created", base);
    send_club(c, id, 201, 1);
}

void clubs_update(Ctx *c, int id) {
    Session s; JObj o; static char out[MAX_FIELDS][FIELD_MAX]; int present[MAX_FIELDS], isnull[MAX_FIELDS], i, any = 0; char t[40];
    if (!auth_require(c, &s, "admin") || !api_read_json(c, &o)) return;
    if (!fields_validate(c, &o, CLUB_FIELDS, NF, 0, out, present, isnull)) { json_free(&o); return; }
    json_free(&o);
    for (i = 0; i < NF; i++) any |= present[i];
    if (!any) { api_error(c, 400, "no_fields", "No valid fields to update."); return; }
    defaults(present, out);
    if (fields_update(c->db, "clubs", id, CLUB_FIELDS, NF, out, present, isnull) != 0) { api_db_error(c); return; }
    if (sqlite3_changes(c->db) == 0) { api_error(c, 404, "not_found", "Club not found."); return; }
    snprintf(t, sizeof t, "club:%d", id);
    audit_log(c->db, s.user_id, "club_updated", t);
    send_club(c, id, 200, 1);
}

/* "Delete" archives the club so past events keep a valid club reference. */
void clubs_archive(Ctx *c, int id) {
    Session s; sqlite3_stmt *st = NULL; char t[40];
    if (!auth_require(c, &s, "admin")) return;
    if (sqlite3_prepare_v2(c->db, "UPDATE clubs SET is_archived=1,updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?1", -1, &st, NULL) == SQLITE_OK) { sqlite3_bind_int(st, 1, id); sqlite3_step(st); }
    sqlite3_finalize(st);
    if (sqlite3_changes(c->db) == 0) { api_error(c, 404, "not_found", "Club not found."); return; }
    snprintf(t, sizeof t, "club:%d", id);
    audit_log(c->db, s.user_id, "club_archived", t);
    api_send(c, 200, "{\"ok\":true,\"archived\":true}", NULL);
}
