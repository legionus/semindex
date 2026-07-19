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
sed "s|@SOURCE_DIR@|$SOURCE_DIR|g" \
	"$SOURCE_DIR/tests/test11.c.search.expect" >"$tmpdir/search.expect"
if ! cmp -s "$tmpdir/search.expect" "$tmpdir/search.out"; then
	diff -u "$tmpdir/search.expect" "$tmpdir/search.out" >&2 || true
	fail "default search output differs"
fi

if ! "$SEMINDEX" search --database "$db" \
	--format='%m|%f|%l|%c|%C|%n|%k|%s\tEND' Outer.y \
	>"$tmpdir/custom.out"; then
	fail "custom field search failed"
fi
sed "s|@SOURCE_DIR@|$SOURCE_DIR|g" \
	"$SOURCE_DIR/tests/test11.c.search-format.expect" >"$tmpdir/custom.expect"
if ! cmp -s "$tmpdir/custom.expect" "$tmpdir/custom.out"; then
	diff -u "$tmpdir/custom.expect" "$tmpdir/custom.out" >&2 || true
	fail "custom search output differs"
fi

if "$SEMINDEX" search --database "$db" --format='%x' Outer.y \
	>"$tmpdir/invalid.out" 2>"$tmpdir/invalid.err"; then
	fail "invalid search format succeeded"
fi
if ! grep -q 'invalid format specification: %x' "$tmpdir/invalid.err"; then
	cat "$tmpdir/invalid.err" >&2
	fail "invalid search format diagnostic differs"
fi

if [ "$(grep -c 'Outer.y' "$tmpdir/custom.out")" != 2 ]; then
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
