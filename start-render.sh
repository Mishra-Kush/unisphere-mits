#!/bin/sh
set -eu

PORT="${PORT:-10000}"
DB_PATH="${UNISPHERE_DB_PATH:-/tmp/unisphere.db}"

: "${UNISPHERE_ADMIN_EMAIL:?Set UNISPHERE_ADMIN_EMAIL in Render Environment}"
: "${UNISPHERE_ADMIN_PASSWORD:?Set UNISPHERE_ADMIN_PASSWORD in Render Environment (at least 12 characters)}"

mkdir -p "$(dirname "$DB_PATH")"

# Generate a private runtime config from Render's environment variables.
printf 'ADMIN_EMAIL=%s\nADMIN_PASSWORD=%s\nDB_PATH=%s\nSCHEMA_PATH=../database/schema.sql\nSEED_PATH=../database/seed.sql\n' \
  "$UNISPHERE_ADMIN_EMAIL" "$UNISPHERE_ADMIN_PASSWORD" "$DB_PATH" \
  > backend/config/render-admin.conf

# Create and seed the SQLite database on first launch; keep the existing admin on later launches.
(cd backend && ./build/init_admin --config config/render-admin.conf --production)

export UNISPHERE_SECURE_COOKIE="${UNISPHERE_SECURE_COOKIE:-1}"
exec ./backend/build/unisphere_server \
  --port "$PORT" \
  --bind 0.0.0.0 \
  --db "$DB_PATH" \
  --web .
