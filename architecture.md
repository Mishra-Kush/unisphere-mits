# Architecture

```
Browser (HTML/CSS/JS, fetch)  --HTTP-->  unisphere_server.exe (C)  --SQL-->  data/unisphere.db (SQLite)
   assets/js/app.js  (API client, header/footer, toasts)       http -> router -> handlers -> SQLite
   public.js / account.js / admin.js (page logic)               auth: cookie session + CSRF header
```

* **Static files:** only top-level `*.html` and `assets/**` are served; everything else returns 404.
* **Auth:** random 256-bit session token in an HttpOnly, SameSite=Strict cookie; only its SHA-256 is stored. Student sessions 8 h, admin 2 h. CSRF token returned at login and required in `X-CSRF-Token` for every state-changing request; `Origin` must match the host.
* **Passwords:** `pbkdf2_sha256$100000$salt$hash`, constant-time compare, dummy hash verified for unknown users.
* **Rate limit:** 5 failed logins per email or 20 per IP in 15 minutes -> HTTP 429.
* **Roles:** `auth_require(c, &session, "admin"|"student")` guards every protected endpoint; student role is forced at registration.
* **Registration:** `BEGIN IMMEDIATE` transaction + UNIQUE(user,event) + a capacity trigger (`EVENT_FULL`) so duplicates/overbooking are impossible even under concurrency.
* **Time:** event dates/times are college-local (IST); registration windows use the server's IST clock.
* **Validation:** every field is checked in C (`fields.c`, `validate.c`); all SQL is parameterised; output is HTML-escaped in the browser (`U.html`) and a strict CSP forbids inline scripts.

## API summary
Auth: `POST /api/auth/student/register|student/login|admin/login|logout`, `GET /api/auth/me`.
Public: `GET /api/clubs[/id[/events]]`, `/api/events[/id]`, `/api/announcements`, `/api/search`.
Student: `POST|DELETE /api/events/{id}/register`, `GET /api/student/registrations`.
Admin: `/api/admin/dashboard`, `/api/admin/clubs[/id]`, `/api/admin/events[/id]`, `/api/admin/events/{id}/registrations[?format=csv]`, `/api/admin/announcements[/id]` (POST, PUT; DELETE archives clubs/events, deletes notices).
