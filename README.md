# UNISPHERE backend (C)

Pure C11 + SQLite. Modules (`src/`): `http` (sockets/HTTP parsing), `json` (flat JSON), `api` (responses, Origin/JSON checks), `auth` (sessions, CSRF, rate limit), `validate`, `fields` (table-driven validation + parameterised INSERT/UPDATE), `clubs`, `events`, `registrations`, `announcements` (+ dashboard), `router`, `password`/`sha256` (PBKDF2-HMAC-SHA256), `init_admin` (database + admin setup), `main`.
Windows: use `..\build.bat`. Linux dev build: `make COMPAT=1 SQLITE_LIB=/path/to/libsqlite3.so` (or put the SQLite amalgamation in `vendor/` and run `make`).
Server options: `--port N --db FILE --web DIR --bind IP`. Private settings: `config/admin.conf` (git-ignored).
