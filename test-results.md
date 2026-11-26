# Test results

Run on: 2026-11-24 in a Linux container (gcc 13, system SQLite 3.45, Chromium via Playwright). **Not run on Windows.**

```
python3 tests/test_db.py -> 12 passed, 0 failed
make -s -C backend COMPAT=1 SQLITE_LIB=<sqlite> test -> 11 passed, 0 failed
bash tests/test_init_admin.sh -> 19 passed, 0 failed
python3 tests/test_api.py -> 58 passed, 0 failed
python3 tests/test_content.py -> 113 passed, 0 failed
python3 tests/test_browser.py -> 62 passed, 0 failed
```

Total: 275 checks passed, 0 failed. Covers: 39 club records, search/filters, calendar, student-domain restriction, wrong/right logins, admin authorisation, sessions/logout/expiry, CSRF, rate limiting, event create/edit/publish/archive, registration/cancel, duplicate prevention, capacity (incl. 6 concurrent requests for 2 seats), announcements, persistence after restart, DB constraints, API error handling, real-browser UI flows (desktop and mobile), strict CSP.

Reproduce: `./run-all-tests.sh`
