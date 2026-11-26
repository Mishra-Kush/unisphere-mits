"""Stage 3 end-to-end tests: clubs, events, registration, announcements, dashboard (real C server)."""
import http.client, json, os, signal, sqlite3, subprocess, sys, tempfile, threading, time
from datetime import datetime, timedelta
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BACK = os.path.join(ROOT, "backend")
T = tempfile.mkdtemp(); DB = os.path.join(T, "t.db"); PORT = 18081
conf = os.path.join(T, "a.conf")
open(conf, "w").write("ADMIN_EMAIL=swiftpark1204@gmail.com\nADMIN_PASSWORD=12120\nDB_PATH=%s\n" % DB)
subprocess.run([BACK + "/build/init_admin", "--config", conf], cwd=BACK, check=True, capture_output=True)
ist = datetime.now(__import__("datetime").timezone.utc).replace(tzinfo=None) + timedelta(hours=5, minutes=30)
D = lambda days: (ist + timedelta(days=days)).strftime("%Y-%m-%d")
TODAY = D(0)

class S:  # a logged-in session
    def __init__(self, cookie=None, csrf=None, uid=None): self.cookie, self.csrf, self.uid = cookie, csrf, uid

def call(method, path, body=None, s=None, csrf=True, raw=None):
    c = http.client.HTTPConnection("127.0.0.1", PORT, timeout=15)
    h = {}
    if body is not None or raw is not None: h["Content-Type"] = "application/json"
    if s and s.cookie: h["Cookie"] = s.cookie
    if s and s.csrf and csrf and method != "GET": h["X-CSRF-Token"] = s.csrf
    c.request(method, path, json.dumps(body) if body is not None else raw, h)
    r = c.getresponse(); txt = r.read().decode()
    try: j = json.loads(txt)
    except ValueError: j = None
    return r.status, j, r, txt

def login(path, email, pw):
    st, j, r, _ = call("POST", path, {"email": email, "password": pw})
    assert st in (200, 201), (st, j)
    return S(r.getheader("Set-Cookie").split(";")[0], j["csrf_token"], j["user"]["id"])

def new_student(n, name=None):
    email = "stu%d@mitsgwl.ac.in" % n
    st, j, r, _ = call("POST", "/api/auth/student/register", {"full_name": name or "Student %d" % n, "email": email, "password": "Passw0rd%d" % n, "confirm_password": "Passw0rd%d" % n})
    assert st == 201, (st, j)
    return S(r.getheader("Set-Cookie").split(";")[0], j["csrf_token"], j["user"]["id"])

def start():
    p = subprocess.Popen([BACK + "/build/unisphere_server", "--port", str(PORT), "--db", DB, "--web", ROOT], cwd=BACK, stdout=subprocess.DEVNULL)
    for _ in range(50):
        try: call("GET", "/api/health"); return p
        except OSError: time.sleep(0.1)
    raise SystemExit("server did not start")

fails = 0
def check(name, cond):
    global fails
    print(("PASS " if cond else "FAIL ") + name); fails += not cond

def code(r): return (r[1] or {}).get("error", {}).get("code")

srv = start()
try:
    admin = login("/api/auth/admin/login", "swiftpark1204@gmail.com", "12120")
    db = sqlite3.connect(DB)
    # ---------- clubs (public) ----------
    st, j, *_ = call("GET", "/api/clubs")
    check("public club list returns all 39 clubs", st == 200 and j["count"] == 39 and len(j["clubs"]) == 39)
    names = [c["name"] for c in j["clubs"]]
    check("club list contains Hindi-named club and every handle has an Instagram URL", "लफ़्ज़-ए-बयां (Open Mic Club)" in names and all(c["instagram_url"].startswith("https://www.instagram.com/") for c in j["clubs"]))
    check("unknown advisor/leads shown as 'Not yet provided'", all(c["faculty_advisor"] == "Not yet provided" and c["recruitment_status"] == "To be announced" for c in j["clubs"]))
    exp = db.execute("select count(*) from clubs where category='technical'").fetchone()[0]
    st, j, *_ = call("GET", "/api/clubs?category=technical")
    check("category filter returns exactly the technical clubs (%d)" % exp, j["count"] == exp and exp > 0 and all(c["category"] == "technical" for c in j["clubs"]))
    st, j, *_ = call("GET", "/api/clubs?q=%40aiclub")
    check("search by Instagram handle finds The AI Club", j["count"] == 1 and "AI Club" in j["clubs"][0]["name"])
    st, j, *_ = call("GET", "/api/clubs?q=robotics")
    check("search by description keyword finds ASIMOV", any("ASIMOV" in c["name"] for c in j["clubs"]))
    st, j, *_ = call("GET", "/api/clubs?q=%27%20OR%201%3D1%20--")
    check("SQL-injection text in search is harmless (0 results)", st == 200 and j["count"] == 0)
    st, j, *_ = call("GET", "/api/clubs?q=%25")
    check("LIKE wildcard % is escaped (0 results)", st == 200 and j["count"] == 0)
    check("club detail works", call("GET", "/api/clubs/1")[1]["club"]["id"] == 1)
    check("missing club -> 404", call("GET", "/api/clubs/9999")[0] == 404)
    check("club list ?limit works", call("GET", "/api/clubs?limit=5")[1]["count"] == 5)

    # ---------- authorization ----------
    NEWCLUB = {"name": "Test Chess Club", "description": "Chess for everyone.", "category": "cultural", "instagram_handle": "chess_mits", "faculty_advisor": "", "recruitment_status": "open"}
    check("anonymous cannot create club (401)", call("POST", "/api/admin/clubs", NEWCLUB)[0] == 401)
    s1 = new_student(1)
    check("student cannot create club (403)", call("POST", "/api/admin/clubs", NEWCLUB, s1)[0] == 403)
    check("student cannot read admin dashboard (403)", call("GET", "/api/admin/dashboard", None, s1)[0] == 403)
    check("admin without CSRF token blocked (403)", call("POST", "/api/admin/clubs", NEWCLUB, admin, csrf=False)[0] == 403)

    # ---------- clubs (admin) ----------
    st, j, *_ = call("POST", "/api/admin/clubs", NEWCLUB, admin)
    cl = j["club"]; CID = cl["id"]
    check("admin creates club (201)", st == 201 and cl["name"] == "Test Chess Club")
    check("new club: handle normalised, blank advisor defaulted, slug set", cl["instagram_handle"] == "@chess_mits" and cl["faculty_advisor"] == "Not yet provided" and cl["slug"].startswith("club-%d-" % CID))
    check("duplicate club name -> 409", call("POST", "/api/admin/clubs", NEWCLUB, admin)[0] == 409)
    check("invalid category -> 400", call("POST", "/api/admin/clubs", dict(NEWCLUB, name="X Club", category="nope"), admin)[0] == 400)
    check("bad Instagram handle -> 400", call("POST", "/api/admin/clubs", dict(NEWCLUB, name="Y Club", instagram_handle="a b/c"), admin)[0] == 400)
    check("path-traversal image path -> 400", call("POST", "/api/admin/clubs", dict(NEWCLUB, name="Z Club", image_path="../../etc/passwd"), admin)[0] == 400)
    check("HTML in name rejected", call("POST", "/api/admin/clubs", dict(NEWCLUB, name="<b>x</b>"), admin)[0] == 400)
    st, j, *_ = call("PUT", "/api/admin/clubs/%d" % CID, {"faculty_advisor": "Dr. Example", "student_leads": "A and B", "recruitment_status": "closed"}, admin)
    check("admin edits advisor/leads/recruitment", st == 200 and j["club"]["faculty_advisor"] == "Dr. Example" and j["club"]["recruitment_status"] == "closed")
    check("update of unknown club -> 404", call("PUT", "/api/admin/clubs/9999", {"student_leads": "x"}, admin)[0] == 404)
    check("empty update -> 400", call("PUT", "/api/admin/clubs/%d" % CID, {"bogus": "x"}, admin)[0] == 400)

    # ---------- events (admin) ----------
    def ev(**kw):
        b = {"club_id": 1, "title": "Sample Workshop", "description": "A clearly labelled TEST event.", "category": "workshop",
             "event_date": D(5), "start_time": "10:00", "end_time": "12:00", "venue": "Test Hall", "is_published": True,
             "registration_enabled": True, "capacity": 3}
        b.update(kw); return b
    check("anonymous cannot create event (401)", call("POST", "/api/admin/events", ev())[0] == 401)
    check("end before start -> 400", call("POST", "/api/admin/events", ev(start_time="12:00", end_time="10:00"), admin)[0] == 400)
    check("impossible date (30 Feb) -> 400", call("POST", "/api/admin/events", ev(event_date="2027-02-30"), admin)[0] == 400)
    check("reg_opens after reg_closes -> 400", call("POST", "/api/admin/events", ev(reg_opens=D(3), reg_closes=D(2)), admin)[0] == 400)
    check("unknown club -> 422", call("POST", "/api/admin/events", ev(club_id=9999), admin)[0] == 422)
    check("capacity 0 -> 400", call("POST", "/api/admin/events", ev(capacity=0), admin)[0] == 400)
    check("script tag in title -> 400", call("POST", "/api/admin/events", ev(title="<script>alert(1)</script>"), admin)[0] == 400)
    check("bad category -> 400", call("POST", "/api/admin/events", ev(category="party"), admin)[0] == 400)
    check("nothing saved by rejected events", db.execute("select count(*) from events").fetchone()[0] == 0)
    st, j, *_ = call("GET", "/api/events")
    check("no events -> empty list (no invented events)", st == 200 and j["count"] == 0 and j["events"] == [])
    mk = lambda **kw: call("POST", "/api/admin/events", ev(**kw), admin)[1]["event"]
    E1 = mk(title="Open Workshop", capacity=3, reg_opens=D(-1), reg_closes=D(4))
    E2 = mk(title="Hidden Draft", is_published=False)
    E3 = mk(title="Registration Off", registration_enabled=False)
    E4 = mk(title="Opens Later", reg_opens=D(2))
    E5 = mk(title="Closed Already", reg_closes=D(-1))
    E6 = mk(title="Past Event", event_date=D(-3))
    E7 = mk(title="Cancelled Event")
    call("PUT", "/api/admin/events/%d" % E7["id"], {"status": "cancelled"}, admin)
    E8 = mk(title="Unlimited Seats", capacity=None, club_id=24, category="competition")
    E9 = mk(title="Started Today", event_date=TODAY, start_time="00:00", end_time="23:59")
    check("created event has expected fields/states", E1["registration_status"] == "open" and E1["capacity"] == 3 and E1["registered_count"] == 0 and E1["is_registered"] is False)
    states = {e["title"]: e["registration_status"] for e in call("GET", "/api/events?limit=100")[1]["events"]}
    check("public list hides unpublished draft", "Hidden Draft" not in states and "Open Workshop" in states)
    check("states: disabled/not_open/closed/ended/cancelled/open/closed(started)",
          [states[k] for k in ["Registration Off", "Opens Later", "Closed Already", "Past Event", "Cancelled Event", "Unlimited Seats", "Started Today"]] == ["disabled", "not_open", "closed", "ended", "cancelled", "open", "closed"])
    check("unpublished event: public 404, admin 200", call("GET", "/api/events/%d" % E2["id"])[0] == 404 and call("GET", "/api/events/%d" % E2["id"], None, admin)[0] == 200)
    check("admin event list includes drafts", any(e["title"] == "Hidden Draft" for e in call("GET", "/api/admin/events?limit=100", None, admin)[1]["events"]))
    check("student cannot use admin event list (403)", call("GET", "/api/admin/events", None, s1)[0] == 403)
    up = [e["title"] for e in call("GET", "/api/events?upcoming=1&limit=100")[1]["events"]]
    check("upcoming=1 excludes the past event", "Past Event" not in up and "Open Workshop" in up)
    check("club filter", [e["title"] for e in call("GET", "/api/events?club=24")[1]["events"]] == ["Unlimited Seats"])
    check("club-category filter (ASIMOV is technical)", "Unlimited Seats" in [e["title"] for e in call("GET", "/api/events?category=technical")[1]["events"]])
    check("event_category filter", [e["title"] for e in call("GET", "/api/events?event_category=competition")[1]["events"]] == ["Unlimited Seats"])
    check("text search over title", [e["title"] for e in call("GET", "/api/events?q=unlimited")[1]["events"]] == ["Unlimited Seats"])
    check("date range filter", [e["title"] for e in call("GET", "/api/events?from=%s&to=%s" % (D(-4), D(-2)))[1]["events"]] == ["Past Event"])
    check("sort=recent returns newest first", call("GET", "/api/events?sort=recent")[1]["events"][0]["title"] == "Started Today")
    check("events of a club endpoint", len(call("GET", "/api/clubs/24/events")[1]["events"]) == 1)
    sr = call("GET", "/api/search?q=asimov")[1]
    check("global search returns clubs and events together", sr["club_count"] == 1 and sr["event_count"] == 1 and sr["events"][0]["title"] == "Unlimited Seats")
    st, j, *_ = call("PUT", "/api/admin/events/%d" % E1["id"], {"venue": "New Venue", "description": "Updated text."}, admin)
    check("admin edits event", st == 200 and j["event"]["venue"] == "New Venue")
    check("event end<=start on update -> 4xx", call("PUT", "/api/admin/events/%d" % E1["id"], {"end_time": "09:00"}, admin)[0] in (400, 422))
    check("update unknown event -> 404", call("PUT", "/api/admin/events/9999", {"venue": "Nowhere"}, admin)[0] == 404)
    call("PUT", "/api/admin/events/%d" % E2["id"], {"is_published": True}, admin)
    check("admin publishes draft", call("GET", "/api/events/%d" % E2["id"])[0] == 200)
    call("PUT", "/api/admin/events/%d" % E2["id"], {"is_published": False}, admin)
    check("admin unpublishes event", call("GET", "/api/events/%d" % E2["id"])[0] == 404)

    # ---------- registration ----------
    R = lambda eid, s=None, **k: call("POST", "/api/events/%d/register" % eid, None, s, **k)
    check("anonymous cannot register (401)", R(E1["id"])[0] == 401)
    check("admin cannot register (403)", R(E1["id"], admin)[0] == 403)
    check("registration without CSRF -> 403", R(E1["id"], s1, csrf=False)[0] == 403)
    check("nothing saved by rejected attempts", db.execute("select count(*) from registrations").fetchone()[0] == 0)
    st, j, *_ = R(E1["id"], s1)
    check("student registers (201) and count increases", st == 201 and j["registration"]["registered_count"] == 1)
    check("registration persisted in SQLite", db.execute("select count(*) from registrations where user_id=? and event_id=?", (s1.uid, E1["id"])).fetchone()[0] == 1)
    st, j, *_ = call("GET", "/api/events/%d" % E1["id"], None, s1)
    check("event detail shows is_registered for that student", j["event"]["is_registered"] is True and j["event"]["registered_count"] == 1)
    check("duplicate registration -> 409 already_registered", code(R(E1["id"], s1)) == "already_registered")
    mine = call("GET", "/api/student/registrations", None, s1)[1]
    check("My Registered Events lists it", mine["count"] == 1 and mine["events"][0]["id"] == E1["id"])
    check("registrations are per-user (another student sees none)", call("GET", "/api/student/registrations", None, new_student(2))[1]["count"] == 0)
    for e, c_ in [(E2, 404), (E3, "registration_disabled"), (E4, "registration_not_open"), (E5, "registration_closed"), (E6, "event_ended"), (E7, "event_cancelled"), (E9, "registration_closed")]:
        r = R(e["id"], s1)
        check("register blocked for '%s' (%s)" % (e["title"], c_), (r[0] == 404) if c_ == 404 else code(r) == c_)
    check("register for missing event -> 404", R(9999, s1)[0] == 404)
    # capacity: E1 has 3 seats, s1 holds one
    s3, s4, s5 = new_student(3), new_student(4), new_student(5)
    check("2nd and 3rd seats succeed", R(E1["id"], s3)[0] == 201 and R(E1["id"], s4)[0] == 201)
    check("4th student -> 409 event_full", code(R(E1["id"], s5)) == "event_full")
    check("event state becomes 'full'", call("GET", "/api/events/%d" % E1["id"])[1]["event"]["registration_status"] == "full")
    check("registrations never exceed capacity", db.execute("select count(*) from registrations where event_id=?", (E1["id"],)).fetchone()[0] == 3)
    check("capacity cannot drop below registrations (422)", call("PUT", "/api/admin/events/%d" % E1["id"], {"capacity": 2}, admin)[0] == 422)
    st, j, *_ = call("DELETE", "/api/events/%d/register" % E1["id"], None, s1)
    check("student cancels registration", st == 200 and db.execute("select count(*) from registrations where event_id=?", (E1["id"],)).fetchone()[0] == 2)
    check("cancel again -> 404 not_registered", code(call("DELETE", "/api/events/%d/register" % E1["id"], None, s1)) == "not_registered")
    check("freed seat can be taken by waiting student", R(E1["id"], s5)[0] == 201)
    check("cancel needs CSRF", call("DELETE", "/api/events/%d/register" % E1["id"], None, s5, csrf=False)[0] == 403)
    # concurrency: 6 students race for 2 seats
    EC = mk(title="Race Event", capacity=2)
    racers = [new_student(10 + i) for i in range(6)]
    results = []
    def go(s): results.append(R(EC["id"], s)[0])
    ts = [threading.Thread(target=go, args=(s,)) for s in racers]
    [t.start() for t in ts]; [t.join() for t in ts]
    check("6 simultaneous registrations for 2 seats -> exactly 2 succeed", sorted(results) == [201, 201, 409, 409, 409, 409] and db.execute("select count(*) from registrations where event_id=?", (EC["id"],)).fetchone()[0] == 2)
    check("unlimited event accepts registrations", R(E8["id"], s1)[0] == 201)

    # ---------- attendees / CSV ----------
    evil = new_student(20, "=SUM(1+1)")
    EA = mk(title="CSV Event", capacity=None); R(EA["id"], evil); R(EA["id"], s1)
    st, j, *_ = call("GET", "/api/admin/events/%d/registrations" % EA["id"], None, admin)
    check("admin sees attendee list", st == 200 and j["count"] == 2 and "email" in j["attendees"][0])
    st, _, r, txt = call("GET", "/api/admin/events/%d/registrations?format=csv" % EA["id"], None, admin)
    check("CSV export works with attachment header", st == 200 and "text/csv" in r.getheader("Content-Type") and "attachment" in r.getheader("Content-Disposition") and txt.startswith("Name,Email"))
    check("CSV formula injection neutralised", "\"'=SUM(1+1)\"" in txt and "\"=SUM" not in txt)
    check("student cannot view attendees (403)", call("GET", "/api/admin/events/%d/registrations" % EA["id"], None, s1)[0] == 403)
    check("attendees of unknown event -> 404", call("GET", "/api/admin/events/9999/registrations", None, admin)[0] == 404)

    # ---------- announcements ----------
    A = lambda **kw: call("POST", "/api/admin/announcements", dict({"title": "Notice title", "message": "Notice body", "priority": "normal", "is_published": True}, **kw), admin)
    check("student cannot publish notices (403)", call("POST", "/api/admin/announcements", {"title": "Fake notice", "message": "x"}, s1)[0] == 403)
    check("anonymous cannot publish notices (401)", call("POST", "/api/admin/announcements", {"title": "Fake notice", "message": "x"})[0] == 401)
    check("invalid priority -> 400", A(priority="critical")[0] == 400)
    check("missing message -> 400", call("POST", "/api/admin/announcements", {"title": "No message"}, admin)[0] == 400)
    n1 = A(title="Normal notice")[1]["announcement"]
    n2 = A(title="URGENT venue change", priority="urgent", event_id=E1["id"])[1]["announcement"]
    n3 = A(title="Draft notice", is_published=False)[1]["announcement"]
    n4 = A(title="Expired notice", expires_at=D(-1))[1]["announcement"]
    n5 = A(title="Expires in future", expires_at=D(3), club_id=1, priority="important")[1]["announcement"]
    check("published notice gets published_at; draft does not", n1["published_at"] and n3["published_at"] is None)
    check("related event/club resolved", n2["event_title"] == "Open Workshop" and n5["club_name"].startswith("Art Club"))
    pub = call("GET", "/api/announcements")[1]["announcements"]
    pt = [a["title"] for a in pub]
    check("public notices hide drafts and expired ones", "Draft notice" not in pt and "Expired notice" not in pt and len(pub) == 3)
    check("urgent notice is listed first, then important", pt[0] == "URGENT venue change" and pt[1] == "Expires in future")
    adm = call("GET", "/api/admin/announcements", None, admin)[1]["announcements"]
    check("admin list keeps drafts and expired records (is_expired flag)", len(adm) == 5 and [a for a in adm if a["title"] == "Expired notice"][0]["is_expired"] is True)
    st, j, *_ = call("PUT", "/api/admin/announcements/%d" % n3["id"], {"is_published": True, "message": "Now live"}, admin)
    check("admin publishes draft notice (published_at stamped)", st == 200 and j["announcement"]["published_at"])
    call("PUT", "/api/admin/announcements/%d" % n1["id"], {"is_published": False}, admin)
    check("admin unpublishes notice", "Normal notice" not in [a["title"] for a in call("GET", "/api/announcements")[1]["announcements"]])
    check("delete notice", call("DELETE", "/api/admin/announcements/%d" % n4["id"], None, admin)[0] == 200 and call("DELETE", "/api/admin/announcements/%d" % n4["id"], None, admin)[0] == 404)

    # ---------- dashboard ----------
    st, j, *_ = call("GET", "/api/admin/dashboard", None, admin)
    sdb = lambda q: db.execute(q).fetchone()[0]
    check("dashboard stats match database", st == 200 and j["stats"]["clubs"] == 40 and j["stats"]["registrations"] == sdb("select count(*) from registrations") and j["stats"]["students"] == sdb("select count(*) from users where role='student'"))
    check("dashboard shows recent activity and announcements", len(j["recent_activity"]) > 0 and len(j["recent_announcements"]) > 0 and j["recent_activity"][0]["by"] == "swiftpark1204@gmail.com")

    # ---------- archive ----------
    before = sdb("select count(*) from registrations where event_id=%d" % E1["id"])
    check("admin archives event", call("DELETE", "/api/admin/events/%d" % E1["id"], None, admin)[0] == 200)
    check("archived event vanishes from public site", call("GET", "/api/events/%d" % E1["id"])[0] == 404 and "Open Workshop" not in [e["title"] for e in call("GET", "/api/events?limit=100")[1]["events"]])
    check("archiving keeps registration records", sdb("select count(*) from registrations where event_id=%d" % E1["id"]) == before)
    check("registering for archived event -> 404", R(E1["id"], new_student(30))[0] == 404)
    check("admin archives club (event history intact)", call("DELETE", "/api/admin/clubs/1", None, admin)[0] == 200 and sdb("select count(*) from events where club_id=1") > 0)
    check("archived club hidden publicly but kept in DB", call("GET", "/api/clubs/1")[0] == 404 and call("GET", "/api/clubs")[1]["count"] == 39 and sdb("select count(*) from clubs") == 40)
    check("admin can still list archived clubs", call("GET", "/api/admin/clubs?all=1", None, admin)[1]["count"] == 40)

    # ---------- persistence ----------
    regs, evs = sdb("select count(*) from registrations"), sdb("select count(*) from events")
    srv.send_signal(signal.SIGTERM); srv.wait()
    srv = start()
    check("events, registrations and notices persist across restart", sdb("select count(*) from registrations") == regs and sdb("select count(*) from events") == evs and call("GET", "/api/events/%d" % E8["id"])[1]["event"]["registered_count"] == 1 and len(call("GET", "/api/announcements")[1]["announcements"]) == 3)
    check("duplicate/overbooking invariants hold in DB", sdb("select count(*) from (select user_id,event_id from registrations group by 1,2 having count(*)>1)") == 0 and sdb("select count(*) from events e where capacity is not null and (select count(*) from registrations r where r.event_id=e.id) > capacity") == 0)
    check("audit log records admin actions", sdb("select count(*) from audit_logs where action in ('event_created','club_created','announcement_created','attendees_exported','event_archived')") >= 10)
finally:
    if srv: srv.terminate()
print("ALL PASSED" if not fails else "%d FAILED" % fails)
sys.exit(1 if fails else 0)
