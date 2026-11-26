#include <string.h>
#include "handlers.h"

#define BAD_METHOD(c) api_error((c), 405, "method_not_allowed", "Method not allowed for this endpoint.")

void api_dispatch(Ctx *c) {
    const char *p = c->req->path, *m = c->req->method, *rest;
    int id, get = !strcmp(m, "GET"), post = !strcmp(m, "POST"), put = !strcmp(m, "PUT"), del = !strcmp(m, "DELETE");

    if (!strcmp(p, "/api/health")) { get ? api_send(c, 200, "{\"ok\":true}", NULL) : BAD_METHOD(c); return; }
    if (!strcmp(p, "/api/auth/me")) { get ? auth_me(c) : BAD_METHOD(c); return; }
    if (!strcmp(p, "/api/auth/student/register")) { post ? auth_student_register(c) : BAD_METHOD(c); return; }
    if (!strcmp(p, "/api/auth/student/login")) { post ? auth_student_login(c) : BAD_METHOD(c); return; }
    if (!strcmp(p, "/api/auth/admin/login")) { post ? auth_admin_login(c) : BAD_METHOD(c); return; }
    if (!strcmp(p, "/api/auth/logout")) { post ? auth_logout(c) : BAD_METHOD(c); return; }

    if (!strcmp(p, "/api/search")) { get ? search_all(c) : BAD_METHOD(c); return; }
    if (!strcmp(p, "/api/clubs")) { get ? clubs_list(c, 0) : BAD_METHOD(c); return; }
    if (path_id(p, "/api/clubs/", &id, &rest)) {
        if (!*rest) { get ? clubs_get(c, id) : BAD_METHOD(c); return; }
        if (!strcmp(rest, "/events")) { get ? clubs_events(c, id) : BAD_METHOD(c); return; }
    }
    if (!strcmp(p, "/api/events")) { get ? events_list(c, 0) : BAD_METHOD(c); return; }
    if (path_id(p, "/api/events/", &id, &rest)) {
        if (!*rest) { get ? events_get(c, id) : BAD_METHOD(c); return; }
        if (!strcmp(rest, "/register")) { post ? reg_register(c, id) : del ? reg_cancel(c, id) : BAD_METHOD(c); return; }
    }
    if (!strcmp(p, "/api/announcements")) { get ? ann_list(c, 0) : BAD_METHOD(c); return; }
    if (!strcmp(p, "/api/student/registrations")) { get ? reg_mine(c) : BAD_METHOD(c); return; }

    if (!strcmp(p, "/api/admin/dashboard")) { get ? admin_dashboard(c) : BAD_METHOD(c); return; }
    if (!strcmp(p, "/api/admin/clubs")) { get ? clubs_list(c, 1) : post ? clubs_create(c) : BAD_METHOD(c); return; }
    if (path_id(p, "/api/admin/clubs/", &id, &rest) && !*rest) { put ? clubs_update(c, id) : del ? clubs_archive(c, id) : BAD_METHOD(c); return; }
    if (!strcmp(p, "/api/admin/events")) { get ? events_list(c, 1) : post ? events_create(c) : BAD_METHOD(c); return; }
    if (path_id(p, "/api/admin/events/", &id, &rest)) {
        if (!*rest) { put ? events_update(c, id) : del ? events_archive(c, id) : BAD_METHOD(c); return; }
        if (!strcmp(rest, "/registrations")) { get ? reg_attendees(c, id) : BAD_METHOD(c); return; }
    }
    if (!strcmp(p, "/api/admin/announcements")) { get ? ann_list(c, 1) : post ? ann_create(c) : BAD_METHOD(c); return; }
    if (path_id(p, "/api/admin/announcements/", &id, &rest) && !*rest) { put ? ann_update(c, id) : del ? ann_delete(c, id) : BAD_METHOD(c); return; }
    api_error(c, 404, "not_found", "Unknown API endpoint.");
}
