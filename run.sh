#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
ROOT="$PWD"
PG_BIN="${PG_BIN:-/usr/lib/postgresql/16/bin}"
DB_PORT="${METRO_DB_PORT:-55439}"
if [ "${1:-}" = stop-db ]; then
  "$PG_BIN/pg_ctl" -D "$ROOT/.runtime/postgres" -m fast stop
  exit
fi
make -j2
./build/metro build-import
if [ -z "${DATABASE_URL:-}" ]; then
  mkdir -p .runtime
  if [ ! -f .runtime/postgres/PG_VERSION ]; then
    "$PG_BIN/initdb" -D "$ROOT/.runtime/postgres" -A trust --no-locale -E UTF8
  fi
  if ! "$PG_BIN/pg_ctl" -D "$ROOT/.runtime/postgres" status >/dev/null 2>&1; then
    "$PG_BIN/pg_ctl" -D "$ROOT/.runtime/postgres" -l "$ROOT/.runtime/postgres.log" \
      -o "-k $ROOT/.runtime -h 127.0.0.1 -p $DB_PORT" start
  fi
  if [ "$("$PG_BIN/psql" -h 127.0.0.1 -p "$DB_PORT" -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname='metro_project_cpp'")" != 1 ]; then
    "$PG_BIN/createdb" -h 127.0.0.1 -p "$DB_PORT" metro_project_cpp
  fi
  export DATABASE_URL="host=127.0.0.1 port=$DB_PORT dbname=metro_project_cpp"
fi
./build/metro init
if [ "$#" -eq 0 ]; then set -- serve; fi
exec ./build/metro "$@"
