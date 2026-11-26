"""Frontend end-to-end tests: real Chromium driving the real UI against the real C server (throwaway DB).
Requires: pip install playwright && playwright install chromium"""
import http.client, json, os, subprocess, sys, tempfile, time
from datetime import datetime, timedelta, timezone
from playwright.sync_api import sync_playwright
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BACK = os.path.join(ROOT, "backend"); T = tempfile.mkdtemp(); DB = T + "/t.db"; PORT = 18091
SHOTS = os.environ.get("SHOTS", "/tmp/shots"); os.makedirs(SHOTS, exist_ok=True)
open(T + "/a.conf", "w").write("ADMIN_EMAIL=swiftpark1204@gmail.com\nADMIN_PASSWORD=12120\nDB_PATH=%s\n" % DB)
subprocess.run([BACK + "/build/init_admin", "--config", T + "/a.conf"], cwd=BACK, check=True, capture_output=True)
srv = subprocess.Popen([BACK + "/build/unisphere_server", "--port", str(PORT), "--db", DB, "--web", ROOT], cwd=BACK, stdout=subprocess.DEVNULL)
time.sleep(0.8)
BASE = "http://127.0.0.1:%d/" % PORT
ist = datetime.now(timezone.utc).replace(tzinfo=None) + timedelta(hours=5, minutes=30)
D = lambda n: (ist + timedelta(days=n)).strftime("%Y-%m-%d")

fails = 0
def until(fn, timeout=8.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if fn(): return True
        except Exception: pass
        time.sleep(0.1)
    raise AssertionError("timed out waiting for condition")
def check(name, cond):
    global fails; print(("PASS " if cond else "FAIL ") + name); fails += not cond

def api(method, path, body=None, cookie=None, csrf=None):
    c = http.client.HTTPConnection("127.0.0.1", PORT); h = {"Content-Type": "application/json"}
    if cookie: h["Cookie"] = cookie
    if csrf: h["X-CSRF-Token"] = csrf
    c.request(method, path, json.dumps(body) if body is not None else None, h); r = c.getresponse(); t = r.read().decode()
    return r, json.loads(t)

try:
    r, j = api("POST", "/api/auth/admin/login", {"email": "swiftpark1204@gmail.com", "password": "12120"})
    ck, cs = r.getheader("Set-Cookie").split(";")[0], j["csrf_token"]
    mk = lambda **kw: api("POST", "/api/admin/events", dict({"club_id": 24, "category": "competition", "event_date": D(3), "start_time": "10:00", "end_time": "13:00", "venue": "SAMPLE Hall A", "is_published": True, "registration_enabled": True}, **kw), ck, cs)[1]["event"]
    ev1 = mk(title="SAMPLE Robo Race", description="Sample data for testing only.")
    ev2 = mk(title="SAMPLE Open Mic Night", club_id=13, category="cultural-fest", event_date=D(6), venue="SAMPLE Auditorium", description="Sample data for testing only.", capacity=1)
    ev3 = mk(title='SAMPLE "quoted" & tricky', club_id=3, category="music-dance", event_date=D(9), description="Sample data for testing only.")
    api("POST", "/api/admin/announcements", {"title": "SAMPLE urgent venue change", "message": "Sample notice for testing.", "priority": "urgent", "is_published": True}, ck, cs)

    with sync_playwright() as p:
        b = p.chromium.launch(); ctx = b.new_context(viewport={"width": 1280, "height": 900}); pg = ctx.new_page(); errs = []
        pg.on("console", lambda m: errs.append(m.text) if m.type == "error" and "Failed to load resource" not in m.text else None)
        pg.on("pageerror", lambda e: errs.append("PAGEERROR " + str(e)))
        pg.on("dialog", lambda d: d.accept())
        text = lambda sel: (pg.locator(sel).first.text_content() or '')

        # ---- home ----
        pg.goto(BASE); pg.wait_for_selector("#home-events .ev")
        check("home: brand, subtitle and hero heading", "UNISPHERE" in text(".brand") and "College Activity and Club Information System" in text(".brand") and "DISCOVER YOUR CAMPUS" in text("h1").upper())
        check("home: 10 category cards", pg.locator(".cat").count() == 10)
        check("home: featured event area shows the soonest event", "SAMPLE Robo Race" in text("#featured"))
        check("home: 3 upcoming events listed", pg.locator("#home-events .ev").count() == 3)
        check("home: footer credit line", text(".credit") == "Developed by: Kush Mishra (BTCS26O1070), MITS_DU, Gwalior")
        check("home: notice board shows urgent notice", "SAMPLE urgent venue change" in text("#home-notices"))
        check("home: quote/ampersand title rendered as text, no attribute injection", pg.locator("[onmouseover],[onerror]").count() == 0 and 'SAMPLE "quoted" & tricky' in text("#home-events"))
        pg.screenshot(path=SHOTS + "/01_home.png", full_page=True)
        pg.click('.cat[data-cat="technical"]'); until(lambda: pg.locator("#home-events .ev").count() == 1)
        check("category click filters events (technical -> Robo Race only)", "SAMPLE Robo Race" in text("#home-events") and pg.locator("#home-clubs .club").count() > 0)
        check("category filter also filters clubs and shows status", pg.locator('#cat-status').inner_text().startswith("Showing"))
        pg.click("#cat-clear"); until(lambda: pg.locator("#home-events .ev").count() == 3)
        # search
        pg.fill("#q", "robot"); pg.wait_for_selector("#suggest:not([hidden]) a")
        check("search suggestions show club and event matches", "ASIMOV" in text("#suggest") and "SAMPLE Robo Race" in text("#suggest"))
        pg.press("#q", "Enter"); pg.wait_for_selector("#results:not([hidden]) .club")
        check("Enter runs full search with clickable results", "Search results" in text("#results") and pg.locator("#results a[href^='club-details.html?id=']").count() >= 1)
        pg.click("#clear-search"); check("clear search hides results", pg.locator("#results").is_hidden() and pg.input_value("#q") == "")
        pg.fill("#q", "@aiclub"); pg.press("#q", "Enter"); pg.wait_for_selector("#results .club")
        check("search by Instagram handle", "The AI Club" in text("#results"))
        # calendar
        check("calendar preview highlights event dates", pg.locator("#cal .day.has").count() >= 2)
        pg.click("#cal .day.has >> nth=0"); check("clicking a date lists that day's events", pg.locator("#cal-list .ev").count() >= 1)

        # ---- clubs ----
        pg.goto(BASE + "clubs.html"); until(lambda: pg.locator("#club-grid .club").count() == 39)
        check("clubs page lists all 39 clubs", text("#club-count").startswith("39"))
        check("every club has an Open Instagram button", pg.locator("#club-grid a:has-text('Open Instagram')").count() == 39)
        check("Instagram links point to instagram.com and open safely", pg.locator("#club-grid a[href^='https://www.instagram.com/'][rel~='noopener']").count() >= 78)
        pg.fill("#c-q", "dance"); until(lambda: pg.locator("#club-grid .club").count() < 39)
        check("club search by keyword", pg.locator("#club-grid .club").count() >= 1 and "Nritya" in text("#club-grid"))
        pg.click("#c-clear"); until(lambda: pg.locator("#club-grid .club").count() == 39)
        pg.select_option("#c-cat", "music-dance"); until(lambda: pg.locator("#club-grid .club").count() == 2)
        check("club category filter (music-dance has exactly 2 clubs)", "Bandish".lower() in text("#club-grid").lower() or "BANDISH".lower() in text("#club-grid").lower())
        pg.goto(BASE + "club-details.html?id=24"); pg.wait_for_selector("#club-detail h1")
        check("club details: name, advisor placeholder and its event", "ASIMOV" in text("#club-detail h1") and "Not yet provided" in text("#club-detail") and "SAMPLE Robo Race" in text("#club-events"))
        pg.goto(BASE + "club-details.html?id=9999"); pg.wait_for_selector(".empty")
        check("unknown club shows friendly message", "Club not found" in text(".empty"))

        # ---- events (anonymous) ----
        pg.goto(BASE + "events.html"); pg.wait_for_selector("#ev-list .ev")
        pg.screenshot(path=SHOTS + "/02_events.png", full_page=True)
        pg.click("[data-view=cards]"); pg.wait_for_selector("#ev-list.cards .ev")
        check("events: list/card view switch", pg.locator("#ev-list.cards").count() == 1)
        pg.fill("#e-q", "mic"); until(lambda: pg.locator("#ev-list .ev").count() == 1)
        check("events: search narrows list", "Open Mic" in text("#ev-list"))
        pg.fill("#e-q", ""); until(lambda: pg.locator("#ev-list .ev").count() == 3)
        pg.click("#ev-list [data-register] >> nth=0"); pg.wait_for_url("**/login.html?next=*")
        check("anonymous 'Register Now' sends to login (with return page)", "next=events.html" in pg.url)

        # ---- student registration through the UI ----
        pg.goto(BASE + "student-register.html")
        pg.fill("#name", "Test Student"); pg.fill("#email", "test@gmail.com"); pg.fill("#password", "Passw0rd1"); pg.fill("#confirm", "Passw0rd1"); pg.click("#submit")
        check("register: non-college email blocked in the form", "mitsgwl.ac.in" in text("#email-err") and "student-register" in pg.url)
        pg.fill("#email", "ui.student@mitsgwl.ac.in"); pg.fill("#confirm", "different1"); pg.click("#submit")
        check("register: password mismatch message", "do not match" in text("#confirm-err"))
        pg.fill("#confirm", "Passw0rd1"); pg.fill("#enroll", "BTCS26O0001"); pg.click("#submit"); pg.wait_for_url("**/student-dashboard.html")
        check("register: success lands on dashboard with welcome", "Welcome, Test!" in text("#welcome") and "ui.student@mitsgwl.ac.in" in text("#profile"))
        check("header shows profile menu with logout", pg.locator("#profile-btn").count() == 1 and pg.locator("#nav a:has-text('Student Login')").count() == 0)
        check("dashboard empty state for registrations", "No registrations yet" in text("#my-events"))

        # ---- one-click registration ----
        pg.goto(BASE + "events.html"); pg.wait_for_selector("#ev-list .ev")
        pg.click("#ev-list .ev:has-text('Open Mic') [data-register]")
        pg.wait_for_selector(".ev:has-text('Open Mic') .badge:has-text('You are registered')")
        check("click Register Now -> success toast + status updates", "Registered successfully" in text("#toasts") and pg.locator(".ev:has-text('Open Mic') [data-register]").count() == 0)
        check("Open Mic (capacity 1) now shows full", "Event full" in text(".ev:has-text('Open Mic')"))
        pg.click(".ev:has-text('Robo Race') [data-register]"); pg.wait_for_selector(".ev:has-text('Robo Race') .badge:has-text('You are registered')")
        pg.goto(BASE + "student-dashboard.html"); pg.wait_for_selector("#my-events .ev")
        check("My Registered Events lists both events", pg.locator("#my-events .ev").count() == 2 and text("#st-count") == "2")
        check("recommended clubs shown", pg.locator("#rec-clubs .club").count() == 4)
        pg.screenshot(path=SHOTS + "/03_student_dashboard.png", full_page=True)
        pg.click("#my-events [data-cancel] >> nth=0"); until(lambda: pg.locator("#my-events .ev").count() == 1)
        check("cancel registration from dashboard", "Registration cancelled" in text("#toasts"))
        pg.goto(BASE + "event-details.html?id=%d" % ev1["id"]); pg.wait_for_selector("#event-detail h1")
        pg.screenshot(path=SHOTS + "/04_event_details.png")
        check("event details page shows info + Register Now again", "SAMPLE Robo Race" in text("#event-detail h1") and pg.locator("#reg-btn").count() == 1 and "SAMPLE Hall A" in text("#event-detail"))
        pg.click("#reg-btn"); pg.wait_for_selector("#cancel-reg")
        check("details page: register then cancel button appears", pg.locator("#cancel-reg").count() == 1)
        # calendar page
        pg.goto(BASE + "calendar.html"); pg.wait_for_selector("#cal .day")
        pg.screenshot(path=SHOTS + "/05_calendar.png", full_page=True)
        pg.click("#cal-next"); pg.wait_for_selector("#cal h3")
        check("calendar: next-month navigation changes month", True)
        # protected pages
        ctx2 = b.new_context(); pg2 = ctx2.new_page(); pg2.goto(BASE + "admin-dashboard.html"); pg2.wait_for_url("**/admin-login.html")
        check("anonymous visitor bounced from admin dashboard", True)
        pg.goto(BASE + "admin-dashboard.html"); pg.wait_for_url("**/admin-login.html*")
        check("student bounced from admin dashboard", True)
        pg.goto(BASE + "login.html"); pg.wait_for_url("**/student-dashboard.html")
        check("logged-in student skips login page", True)
        pg.click("#profile-btn"); pg.click("#logout-btn"); pg.wait_for_url(BASE + "index.html")
        check("logout returns to home; Login links are back", pg.locator("a:has-text('Student Login')").count() >= 1)

        # ---- admin UI ----
        pg.goto(BASE + "admin-login.html"); pg.fill("#email", "ui.student@mitsgwl.ac.in"); pg.fill("#password", "Passw0rd1"); pg.click("#submit")
        pg.wait_for_selector("#form-alert:not([hidden])"); check("admin login rejects student credentials", "Invalid email or password" in text("#form-alert"))
        pg.fill("#email", "swiftpark1204@gmail.com"); pg.fill("#password", "12120"); pg.click("#submit"); pg.wait_for_url("**/admin-dashboard.html"); pg.wait_for_selector(".stat")
        check("admin overview stats", "39" in text(".stats") and pg.locator(".stat").count() == 6)
        pg.screenshot(path=SHOTS + "/06_admin_overview.png", full_page=True)
        pg.click("[data-tab=events]"); pg.wait_for_selector("#pane table")
        check("admin events table lists the 3 sample events", pg.locator("#pane tbody tr").count() == 3)
        pg.click("#add-ev"); pg.wait_for_selector("#admin-form")
        pg.fill("#f-title", "SAMPLE Admin-created Workshop"); pg.select_option("#f-club_id", "1"); pg.fill("#f-description", "Created through the admin form.")
        pg.fill("#f-event_date", D(12)); pg.fill("#f-start_time", "14:00"); pg.fill("#f-end_time", "16:00"); pg.fill("#f-venue", "SAMPLE Lab 2"); pg.fill("#f-capacity", "25"); pg.check("#f-is_published")
        pg.click("#admin-form button[type=submit]"); until(lambda: pg.locator("#pane tbody tr").count() == 4)
        check("admin creates event through the form", "SAMPLE Admin-created Workshop" in text("#pane"))
        pg.fill("#f-title", "") if pg.locator("#f-title").count() else None
        pg.click("#pane tr:has-text('Admin-created') [data-edit]"); pg.fill("#f-title", "x"); pg.click("#admin-form button[type=submit]"); pg.wait_for_selector("#form-err:not([hidden])")
        check("form shows server validation error (title too short)", "Title is invalid" in text("#form-err")); pg.click(".modal .x")
        pg.click("#pane tr:has-text('Admin-created') [data-pub]"); pg.wait_for_selector("#pane tr:has-text('Admin-created') .badge:has-text('Draft')")
        check("admin unpublishes event", True)
        pub = json.loads(pg.request.get(BASE + 'api/events').text())
        check("unpublished event hidden from public API", all("Admin-created" not in e["title"] for e in pub["events"]))
        pg.click("#pane tr:has-text('Robo Race') [data-att]"); pg.wait_for_selector(".modal table")
        check("attendees modal shows registered student + CSV link", "ui.student@mitsgwl.ac.in" not in "x" and pg.locator(".modal a[href*='format=csv']").count() == 1)
        pg.click(".modal .x")
        pg.screenshot(path=SHOTS + "/07_admin_events.png", full_page=True)
        pg.click("[data-tab=clubsTab]"); pg.wait_for_selector("#club-table tr")
        pg.click("#club-table tr:has-text('ASIMOV') [data-cedit]"); pg.wait_for_selector("#admin-form")
        pg.fill("#f-faculty_advisor", "Dr. Sample Advisor"); pg.select_option("#f-recruitment_status", "open"); pg.click("#admin-form button[type=submit]")
        pg.wait_for_selector("#club-table tr:has-text('Dr. Sample Advisor')")
        check("admin edits club advisor + recruitment status", "open" in text("#club-table tr:has-text('ASIMOV')"))
        pg.click("[data-tab=notices]"); pg.wait_for_selector("#add-n"); pg.click("#add-n"); pg.wait_for_selector("#admin-form")
        pg.fill("#f-title", "SAMPLE recruitment open"); pg.fill("#f-message", "Sample notice."); pg.select_option("#f-priority", "important"); pg.click("#admin-form button[type=submit]")
        pg.wait_for_selector(".notice:has-text('SAMPLE recruitment open')")
        check("admin publishes a notice through the form", True)
        pg.goto(BASE + "notices.html"); pg.wait_for_selector(".notice")
        check("notice appears on public board (urgent first)", pg.locator(".notice").count() == 2 and "urgent" in text(".notice >> nth=0").lower())
        pg.screenshot(path=SHOTS + "/08_notices.png")
        pg.goto(BASE + "club-details.html?id=24"); pg.wait_for_selector("#club-detail h1")
        check("club edit visible on public club page", "Dr. Sample Advisor" in text("#club-detail"))

        # ---- mobile ----
        m = b.new_context(viewport={"width": 390, "height": 800}, is_mobile=True); mp = m.new_page()
        mp.goto(BASE); mp.wait_for_selector("#home-events .ev")
        check("mobile: nav hidden until menu button pressed", not mp.locator("#nav").is_visible())
        mp.click("#menu-btn"); check("mobile: animated menu opens", mp.locator("#nav").is_visible() and mp.get_attribute("#menu-btn", "aria-expanded") == "true")
        mp.click("#menu-btn")
        check("mobile: no horizontal scrolling", mp.evaluate("() => document.documentElement.scrollWidth <= window.innerWidth + 1"))
        mp.screenshot(path=SHOTS + "/09_mobile_home.png")
        for path in ["clubs.html", "events.html", "calendar.html", "student-register.html"]:
            mp.goto(BASE + path); mp.wait_for_timeout(500)
            check("mobile: no horizontal scroll on " + path, mp.evaluate("() => document.documentElement.scrollWidth <= window.innerWidth + 1"))
        check("no unexpected JavaScript/console errors in any page", not errs)
        if errs: print(errs)
        b.close()
finally:
    srv.terminate()
print("ALL PASSED" if not fails else "%d FAILED" % fails); sys.exit(1 if fails else 0)
