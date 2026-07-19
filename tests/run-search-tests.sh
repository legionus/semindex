#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

if [ -z "${SEMINDEX:-}" ] || [ -z "${COMPILE_COMMANDS:-}" ] ||
   [ -z "${SOURCE_DIR:-}" ]; then
	fail "SEMINDEX, COMPILE_COMMANDS, and SOURCE_DIR must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
db=$tmpdir/.semindex/semindex.db

"$SEMINDEX" index --database "$db" --compile-commands "$COMPILE_COMMANDS" \
	"$SOURCE_DIR/tests/test11.c" >/dev/null

if ! "$SEMINDEX" search --database "$db" Outer.y >"$tmpdir/search.out"; then
	fail "exact field search failed"
fi
if [ "$(grep -c 'Outer.y' "$tmpdir/search.out")" != 2 ]; then
	cat "$tmpdir/search.out" >&2
	fail "exact field search returned incomplete results"
fi

plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN SELECT files.path FROM records JOIN files ON files.id = records.file_id WHERE records.symbol = 'Outer.y'")
if ! printf '%s\n' "$plan" | grep -q 'SEARCH records USING PRIMARY KEY (symbol=?)'; then
	printf '%s\n' "$plan" >&2
	fail "exact symbol search does not use the primary key"
fi
if printf '%s\n' "$plan" | grep -q 'SCAN records'; then
	printf '%s\n' "$plan" >&2
	fail "exact symbol search scans all records"
fi
if [ "$(sqlite3 "$db" 'PRAGMA journal_mode')" != wal ]; then
	fail "index database is not using WAL"
fi
