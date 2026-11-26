"""End-to-end API tests: starts the real C server on a temp DB and talks HTTP to it."""
import http.client, json, os, signal, sqlite3, subprocess, sys, tempfile, time, hashlib
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BACK = os.path.join(ROOT, "backend")
T = tempfile.mkdtemp(); DB = os.path.join(T, "t.db"); PORT = 18080
conf = os.path.join(T, "a.conf")
open(conf, "w").write("ADMIN_EMAIL=swiftpark1204@gmail.com\nADMIN_PASSWORD=12120\nDB_PATH=%s\n" % DB)
subprocess.run([BACK + "/build/init_admin", "--config", conf], cwd=BACK, check=True, capture_output=True)

def start():
    p = subprocess.Popen([BACK + "/build/unisphere_server", "--port", str(PORT), "--db", DB, "--web", ROOT],
                         cwd=BACK, stdout=subprocess.DEVNULL)
    for _ in range(50):
        try: call("GET", "/api/health"); return p
        except OSError: time.sleep(0.1)
    raise SystemExit("server did not start")

def call(method, path, body=None, cookie=None, csrf=None, ctype="application/json", origin=None, raw=None):
    c = http.client.HTTPConnection("127.0.0.1", PORT, timeout=10)
    h = {}
    if body is not None or raw is not None: h["Content-Type"] = ctype
    if cookie: h["Cookie"] = cookie
    if csrf: h["X-CSRF-Token"] = csrf
    if origin: h["Origin"] = origin
    data = raw if raw is not None else (json.dumps(body) if body is not None else None)
    c.request(method, path, data, h)
    r = c.getresponse(); txt = r.read().decode()
    try: j = json.loads(txt)
    except ValueError: j = None
    return r.status, j, r, txt

def cookie_of(r):
    sc = r.getheader("Set-Cookie") or ""
    return sc.split(";")[0] if sc else None

fails = 0
def check(name, cond):
    global fails
    print(("PASS " if cond else "FAIL ") + name); fails += not cond

srv = start()
try:
    GOOD = {"full_name": "Test Student", "email": "Test.Student@mitsgwl.ac.in", "enrollment_no": "btcs26o9999",
            "password": "Passw0rdOK", "confirm_password": "Passw0rdOK"}
    check("health endpoint", call("GET", "/api/health")[0] == 200)
    check("unknown endpoint -> 404 JSON", call("GET", "/api/nope")[0] == 404)
    check("wrong method -> 405", call("GET", "/api/auth/student/login")[0] == 405)
    for p in ["/backend/config/admin.conf", "/../backend/config/admin.conf", "/data/t.db", "/database/schema.sql",
              "/backend/src/auth.c", "/assets/../backend/Makefile", "/%2e%2e/backend/Makefile", "/.gitignore"]:
        check("static blocks " + p, call("GET", p)[0] in (404, 400))
    # --- registration validation ---
    s, j, r, _ = call("POST", "/api/auth/student/register", dict(GOOD, email="x@gmail.com"))
    check("register: non-college email -> 400 invalid_domain", s == 400 and j["error"]["code"] == "invalid_domain")
    s, j, *_ = call("POST", "/api/auth/student/register", dict(GOOD, email="x@mitsgwl.ac.in.evil.com"))
    check("register: look-alike domain rejected", s == 400)
    s, j, *_ = call("POST", "/api/auth/student/register", dict(GOOD, email="x@sub.mitsgwl.ac.in"))
    check("register: subdomain rejected", s == 400)
    check("register: short/weak password -> 400", call("POST", "/api/auth/student/register", dict(GOOD, password="abc", confirm_password="abc"))[0] == 400)
    check("register: letters-only password -> 400", call("POST", "/api/auth/student/register", dict(GOOD, password="abcdefghij", confirm_password="abcdefghij"))[0] == 400)
    s, j, *_ = call("POST", "/api/auth/student/register", dict(GOOD, confirm_password="Different1"))
    check("register: mismatch -> 400", s == 400 and j["error"]["code"] == "password_mismatch")
    check("register: missing name -> 400", call("POST", "/api/auth/student/register", {k: v for k, v in GOOD.items() if k != "full_name"})[0] == 400)
    check("register: <script> name rejected", call("POST", "/api/auth/student/register", dict(GOOD, full_name="<script>x</script>"))[0] == 400)
    check("register: bad enrollment rejected", call("POST", "/api/auth/student/register", dict(GOOD, enrollment_no="a b!"))[0] == 400)
    check("register: invalid JSON -> 400", call("POST", "/api/auth/student/register", raw="{bad json")[0] == 400)
    check("register: nested JSON rejected", call("POST", "/api/auth/student/register", raw='{"a":{"b":1}}')[0] == 400)
    check("register: non-JSON content type -> 415", call("POST", "/api/auth/student/register", raw="a=b", ctype="text/plain")[0] == 415)
    check("register: foreign Origin -> 403", call("POST", "/api/auth/student/register", GOOD, origin="http://evil.example")[0] == 403)
    check("register: oversized body -> 413", call("POST", "/api/auth/student/register", raw="x" * 70000)[0] == 413)
    check("nothing created by rejected requests", sqlite3.connect(DB).execute("select count(*) from users where role='student'").fetchone()[0] == 0)
    # --- successful registration ---
    s, j, r, _ = call("POST", "/api/auth/student/register", dict(GOOD, role="admin"), origin="http://127.0.0.1:%d" % PORT)
    sc = r.getheader("Set-Cookie") or ""
    check("register: success 201, role forced to student despite role=admin", s == 201 and j["user"]["role"] == "student")
    check("register: email lowercased, enrollment uppercased", j["user"]["email"] == "test.student@mitsgwl.ac.in" and j["user"]["enrollment_no"] == "BTCS26O9999")
    check("register: response says email NOT verified", j["user"]["email_verified"] is False)
    check("cookie flags HttpOnly + SameSite=Strict + Max-Age=28800", "HttpOnly" in sc and "SameSite=Strict" in sc and "Max-Age=28800" in sc)
    check("no password hash in response", "pbkdf2" not in json.dumps(j))
    ck, csrf = cookie_of(r), j["csrf_token"]
    token = ck.split("=")[1]
    db = sqlite3.connect(DB)
    check("session stored as SHA-256 hash, not raw token", db.execute("select count(*) from sessions where token_hash=?", (hashlib.sha256(token.encode()).hexdigest(),)).fetchone()[0] == 1 and db.execute("select count(*) from sessions where token_hash=?", (token,)).fetchone()[0] == 0)
    s, *_ = call("POST", "/api/auth/student/register", dict(GOOD, email="other@mitsgwl.ac.in"))
    check("duplicate enrollment -> 409", s == 409)
    s, *_ = call("POST", "/api/auth/student/register", dict(GOOD, enrollment_no="OTHER123", email="TEST.student@mitsgwl.ac.in"))
    check("duplicate email (any case) -> 409", s == 409)
    # --- session ---
    s, j, *_ = call("GET", "/api/auth/me", cookie=ck)
    check("me with session -> 200 student", s == 200 and j["user"]["email"] == "test.student@mitsgwl.ac.in")
    check("me without session -> 401", call("GET", "/api/auth/me")[0] == 401)
    check("me with forged cookie -> 401", call("GET", "/api/auth/me", cookie="unisphere_sid=" + "a" * 64)[0] == 401)
    check("me with malformed cookie -> 401", call("GET", "/api/auth/me", cookie="unisphere_sid=../../x")[0] == 401)
    # --- logout / csrf ---
    s, j, *_ = call("POST", "/api/auth/logout", cookie=ck)
    check("logout without CSRF token -> 403", s == 403)
    check("still logged in after rejected logout", call("GET", "/api/auth/me", cookie=ck)[0] == 200)
    check("logout with wrong CSRF token -> 403", call("POST", "/api/auth/logout", cookie=ck, csrf="0" * 32)[0] == 403)
    s, j, r, _ = call("POST", "/api/auth/logout", cookie=ck, csrf=csrf)
    check("logout with CSRF -> 200 and cookie cleared", s == 200 and "Max-Age=0" in (r.getheader("Set-Cookie") or ""))
    check("old cookie invalid after logout", call("GET", "/api/auth/me", cookie=ck)[0] == 401)
    # --- student login ---
    L = "/api/auth/student/login"
    check("student login: wrong password -> 401", call("POST", L, {"email": "test.student@mitsgwl.ac.in", "password": "nope"})[0] == 401)
    s, j, *_ = call("POST", L, {"email": "ghost@mitsgwl.ac.in", "password": "Passw0rdOK"})
    check("student login: unknown user -> same 401 message", s == 401 and j["error"]["message"] == "Invalid email or password.")
    s, j, *_ = call("POST", L, {"email": "someone@gmail.com", "password": "x"})
    check("student login: non-college email -> 400 (C backend check)", s == 400 and j["error"]["code"] == "invalid_domain")
    s, *_ = call("POST", L, {"email": "swiftpark1204@gmail.com", "password": "12120"})
    check("student login: admin credentials rejected", s == 400)
    s, j, r, _ = call("POST", L, {"email": "Test.Student@mitsgwl.ac.in", "password": "Passw0rdOK"})
    check("student login: correct -> 200", s == 200 and j["user"]["role"] == "student")
    # --- admin login ---
    A = "/api/auth/admin/login"
    s, *_ = call("POST", A, {"email": "test.student@mitsgwl.ac.in", "password": "Passw0rdOK"})
    check("admin login: student account rejected (401)", s == 401)
    check("admin login: wrong password -> 401", call("POST", A, {"email": "swiftpark1204@gmail.com", "password": "wrong"})[0] == 401)
    check("admin login: wrong email -> 401", call("POST", A, {"email": "other@gmail.com", "password": "12120"})[0] == 401)
    s, j, r, _ = call("POST", A, {"email": "swiftpark1204@gmail.com", "password": "12120"})
    check("admin login: correct -> 200 role admin", s == 200 and j["user"]["role"] == "admin")
    check("admin cookie Max-Age=7200 (2h)", "Max-Age=7200" in (r.getheader("Set-Cookie") or ""))
    ack = cookie_of(r)
    s, j, *_ = call("GET", "/api/auth/me", cookie=ack)
    check("admin session recognised", s == 200 and j["user"]["role"] == "admin")
    # --- expiry ---
    db.execute("update sessions set expires_at=1"); db.commit()
    check("expired sessions rejected", call("GET", "/api/auth/me", cookie=ack)[0] == 401)
    # --- rate limiting ---
    for i in range(5):
        s, *_ = call("POST", A, {"email": "swiftpark1204@gmail.com", "password": "bad%d" % i})
    s, j, r, _ = call("POST", A, {"email": "swiftpark1204@gmail.com", "password": "12120"})
    check("rate limit: 6th attempt blocked (429) even with correct password", s == 429 and r.getheader("Retry-After") == "900")
    # --- persistence across restart ---
    srv.send_signal(signal.SIGTERM); srv.wait(); srv = None
    db.execute("delete from login_attempts"); db.commit()
    srv = start()
    s, j, *_ = call("POST", L, {"email": "test.student@mitsgwl.ac.in", "password": "Passw0rdOK"})
    check("data persists after server restart (student can log in)", s == 200)
    s, j, *_ = call("POST", A, {"email": "swiftpark1204@gmail.com", "password": "12120"})
    check("admin persists after server restart", s == 200)
finally:
    if srv: srv.terminate()
print("ALL PASSED" if not fails else "%d FAILED" % fails)
sys.exit(1 if fails else 0)
