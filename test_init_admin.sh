#!/bin/bash
# End-to-end test of backend/build/init_admin using a throw-away database.
cd "$(dirname "$0")/../backend" || exit 1
T=$(mktemp -d); DB="$T/test.db"; fails=0
ok(){ if [ "$2" = 0 ]; then echo "PASS $1"; else echo "FAIL $1"; fails=$((fails+1)); fi; }
mkconf(){ printf 'ADMIN_EMAIL=%s\nADMIN_PASSWORD=%s\nDB_PATH=%s\n' "$1" "$2" "$DB" > "$T/a.conf"; }
q(){ python3 -c "import sqlite3,sys;print(sqlite3.connect('$DB').execute(sys.argv[1]).fetchone()[0])" "$1"; }
BIN=./build/init_admin

mkconf "swiftpark1204@gmail.com" "12120"
$BIN --config "$T/a.conf" >"$T/o1" 2>"$T/e1"; ok "first run succeeds" $?
ok "39 clubs seeded by tool" $([ "$(q 'select count(*) from clubs')" = 39 ]; echo $?)
ok "exactly one admin with role=admin" $([ "$(q "select count(*) from users where role='admin'")" = 1 ]; echo $?)
ok "email stored normalised" $([ "$(q "select email from users where role='admin'")" = "swiftpark1204@gmail.com" ]; echo $?)
ok "weak-password warning shown" $(grep -q WARNING "$T/e1"; echo $?)
ok "plaintext password not in DB file" $(! grep -aq "12120" "$DB"; echo $?)
ok "plaintext/hash not printed to console" $(! grep -Eq "12120|pbkdf2" "$T/o1" "$T/e1"; echo $?)
ok "stored hash verifies with independent Python PBKDF2" $(python3 - "$DB" <<'PY'
import sqlite3,sys,hashlib
h=sqlite3.connect(sys.argv[1]).execute("select password_hash from users where role='admin'").fetchone()[0]
alg,it,salt,dk=h.split('$')
ok=alg=="pbkdf2_sha256" and hashlib.pbkdf2_hmac('sha256',b'12120',bytes.fromhex(salt),int(it)).hex()==dk
bad=hashlib.pbkdf2_hmac('sha256',b'wrong',bytes.fromhex(salt),int(it)).hex()!=dk
sys.exit(0 if ok and bad else 1)
PY
echo $?)
ok "audit log entry written" $([ "$(q "select count(*) from audit_logs where action='admin_created'")" = 1 ]; echo $?)

H1=$(q "select password_hash from users where role='admin'")
$BIN --config "$T/a.conf" >"$T/o2" 2>/dev/null; ok "second run succeeds" $?
ok "second run reports 'already exists'" $(grep -q "already exists" "$T/o2"; echo $?)
ok "second run changed nothing (no duplicate, same hash)" $([ "$(q 'select count(*) from users')" = 1 ] && [ "$(q "select password_hash from users")" = "$H1" ]; echo $?)

python3 -c "import sqlite3;d=sqlite3.connect('$DB');d.execute(\"insert into sessions values('t',1,'c',0,9999999999)\");d.commit()"
mkconf "swiftpark1204@gmail.com" "a-much-longer-password-1"
$BIN --config "$T/a.conf" --reset >/dev/null 2>&1; ok "--reset succeeds" $?
ok "--reset changed hash and revoked sessions" $([ "$(q "select password_hash from users")" != "$H1" ] && [ "$(q 'select count(*) from sessions')" = 0 ]; echo $?)
ok "--reset kept a single admin" $([ "$(q "select count(*) from users where role='admin'")" = 1 ]; echo $?)

mkconf "not-an-email" "a-much-longer-password-1"
$BIN --config "$T/a.conf" --reset >/dev/null 2>&1; ok "invalid email rejected" $([ $? != 0 ]; echo $?)
rm -f "$DB"; mkconf "swiftpark1204@gmail.com" "12120"
$BIN --config "$T/a.conf" --production >/dev/null 2>&1; ok "--production refuses weak password" $([ $? != 0 ]; echo $?)
ok "refused run created no admin" $([ ! -f "$DB" ] || [ "$(q "select count(*) from users")" = 0 ]; echo $?)
$BIN --config "$T/missing.conf" >/dev/null 2>&1; ok "missing config reported as error" $([ $? != 0 ]; echo $?)
rm -rf "$T"
[ $fails = 0 ] && echo "ALL PASSED" || echo "$fails FAILED"; exit $fails
