import sqlite3, sys
db = sqlite3.connect(":memory:"); db.execute("PRAGMA foreign_keys=ON")
db.executescript(open("database/schema.sql", encoding="utf-8").read())
db.executescript(open("database/seed.sql", encoding="utf-8").read())
ok = True
def check(name, cond):
    global ok; ok &= bool(cond); print(("PASS " if cond else "FAIL ") + name)
def fails(sql, args=()):
    try: db.execute(sql, args); return False
    except sqlite3.IntegrityError: return True
EV = "insert into events(club_id,title,description,category,event_date,start_time,end_time,venue) values(%s,'Test event','d','workshop','2030-01-01','%s','%s','V')"
check("39 clubs seeded", db.execute("select count(*) from clubs").fetchone()[0] == 39)
check("39 unique Instagram handles", db.execute("select count(distinct instagram_handle) from clubs").fetchone()[0] == 39)
check("advisor/leads/recruitment use 'not provided' defaults", db.execute("select count(*) from clubs where faculty_advisor!='Not yet provided' or student_leads!='Not yet provided' or recruitment_status!='To be announced'").fetchone()[0] == 0)
check("no invented events seeded", db.execute("select count(*) from events").fetchone()[0] == 0)
check("student with non-college email rejected", fails("insert into users(full_name,email,password_hash) values('Test A','a@gmail.com','x')"))
db.execute("insert into users(full_name,email,enrollment_no,password_hash) values('Test A','a@mitsgwl.ac.in','E1','x')")
check("duplicate email (case-insensitive) rejected", fails("insert into users(full_name,email,password_hash) values('Test B','A@MITSGWL.AC.IN','x')"))
check("duplicate enrollment number rejected", fails("insert into users(full_name,email,enrollment_no,password_hash) values('Test B','b@mitsgwl.ac.in','e1','x')"))
check("event with unknown club rejected", fails(EV % (999,'10:00','11:00')))
check("event end time <= start time rejected", fails(EV % (1,'11:00','10:00')))
db.execute(EV % (1,'10:00','11:00'))
db.execute("insert into registrations(user_id,event_id) values(1,1)")
check("duplicate registration rejected", fails("insert into registrations(user_id,event_id) values(1,1)"))
db.execute("insert into events(club_id,title,description,category,event_date,start_time,end_time,venue,capacity) values(1,'Cap event','d','workshop','2030-01-02','10:00','11:00','V',1)")
db.execute("insert into users(full_name,email,password_hash) values('Test C','c@mitsgwl.ac.in','x')")
db.execute("insert into registrations(user_id,event_id) values(1,2)")
check("capacity trigger blocks overbooking", fails("insert into registrations(user_id,event_id) values(2,2)"))
check("club with events cannot be hard-deleted", fails("delete from clubs where id=1"))
sys.exit(0 if ok else 1)
