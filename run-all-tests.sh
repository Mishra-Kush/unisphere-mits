#!/bin/bash
# Developer test runner (Linux/macOS/WSL). Needs: gcc, python3, libsqlite3; browser tests also need `pip install playwright && playwright install chromium`.
cd "$(dirname "$0")"
SQLITE_LIB=${SQLITE_LIB:-/usr/lib/x86_64-linux-gnu/libsqlite3.so.0}
make -C backend COMPAT=1 SQLITE_LIB="$SQLITE_LIB" >/dev/null || exit 1
rc=0
for t in "python3 tests/test_db.py" "make -s -C backend COMPAT=1 SQLITE_LIB=$SQLITE_LIB test" "bash tests/test_init_admin.sh" "python3 tests/test_api.py" "python3 tests/test_content.py" "python3 tests/test_browser.py"; do
  out=$($t 2>&1); p=$(echo "$out" | grep -c '^PASS'); f=$(echo "$out" | grep -c '^FAIL')
  echo "$t -> $p passed, $f failed"; [ "$f" != 0 ] && rc=1
done
exit $rc
