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
"$SEMINDEX" index --database "$db" --compile-commands "$COMPILE_COMMANDS" \
	"$SOURCE_DIR/tests/test15.c" >/dev/null

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

"$SEMINDEX" index --variant=arm64-defconfig --database "$db" \
	--compile-commands "$COMPILE_COMMANDS" "$SOURCE_DIR/tests/test11.c" \
	>/dev/null
if ! "$SEMINDEX" search --database "$db" \
	--format='%v|%F|%f|%n' Outer.y >"$tmpdir/variant.out"; then
	fail "multi-variant search failed"
fi
sed "s|@SOURCE_DIR@|$SOURCE_DIR|g" \
	"$SOURCE_DIR/tests/test11.c.search-variant.expect" >"$tmpdir/variant.expect"
if ! cmp -s "$tmpdir/variant.expect" "$tmpdir/variant.out"; then
	diff -u "$tmpdir/variant.expect" "$tmpdir/variant.out" >&2 || true
	fail "multi-variant search output differs"
fi

if ! "$SEMINDEX" search --database "$db" --variant='arm64-*' \
	--format='%v' Outer.y >"$tmpdir/variant-filter.out"; then
	fail "variant-filtered search failed"
fi
if [ "$(wc -l <"$tmpdir/variant-filter.out")" != 2 ] || \
	grep -qv '^arm64-defconfig$' "$tmpdir/variant-filter.out"; then
	cat "$tmpdir/variant-filter.out" >&2
	fail "variant filter returned unexpected results"
fi

if "$SEMINDEX" search --database "$db" --format='%x' Outer.y \
	>"$tmpdir/invalid.out" 2>"$tmpdir/invalid.err"; then
	fail "invalid search format succeeded"
fi
if ! grep -q 'invalid format specification: %x' "$tmpdir/invalid.err"; then
	cat "$tmpdir/invalid.err" >&2
	fail "invalid search format diagnostic differs"
fi

if ! "$SEMINDEX" search --database "$db" --variant=general --format='%m' --mode=def \
	Outer.y >"$tmpdir/mode-def.out"; then
	fail "definition mode search failed"
fi
if [ "$(cat "$tmpdir/mode-def.out")" != def ]; then
	cat "$tmpdir/mode-def.out" >&2
	fail "definition mode search returned unexpected results"
fi

if ! "$SEMINDEX" search --database "$db" --variant=general --format='%m' --mode=w \
	Outer.y >"$tmpdir/mode-write.out"; then
	fail "write mode search failed"
fi
if [ "$(cat "$tmpdir/mode-write.out")" != -w- ]; then
	cat "$tmpdir/mode-write.out" >&2
	fail "write mode search returned unexpected results"
fi

if ! "$SEMINDEX" search --database "$db" --variant=general --format='%m' --mode=-w- \
	Outer.y >"$tmpdir/mode-exact.out"; then
	fail "three-character mode search failed"
fi
if ! cmp -s "$tmpdir/mode-write.out" "$tmpdir/mode-exact.out"; then
	diff -u "$tmpdir/mode-write.out" "$tmpdir/mode-exact.out" >&2 || true
	fail "three-character mode search returned unexpected results"
fi

if ! "$SEMINDEX" search --database "$db" --variant=general --mode=r Outer.y \
	>"$tmpdir/mode-read.out"; then
	fail "read mode search failed"
fi
if [ -s "$tmpdir/mode-read.out" ]; then
	cat "$tmpdir/mode-read.out" >&2
	fail "read mode search returned a write operation"
fi

if ! "$SEMINDEX" search --database "$db" --variant=general --mode=- Outer.y \
	>"$tmpdir/mode-none.out"; then
	fail "empty mode search failed"
fi
if [ -s "$tmpdir/mode-none.out" ]; then
	cat "$tmpdir/mode-none.out" >&2
	fail "empty mode search returned symbol records"
fi

if "$SEMINDEX" search --database "$db" --mode=invalid Outer.y \
	>"$tmpdir/invalid-mode.out" 2>"$tmpdir/invalid-mode.err"; then
	fail "invalid search mode succeeded"
fi
if ! grep -q 'invalid mode: invalid' "$tmpdir/invalid-mode.err"; then
	cat "$tmpdir/invalid-mode.err" >&2
	fail "invalid search mode diagnostic differs"
fi

if ! "$SEMINDEX" search --database "$db" --mode=w \
	--format='%m|%l|%c|%C|%n' task_struct.pid \
	>"$tmpdir/macro-write.out"; then
	fail "macro argument write search failed"
fi
if [ "$(cat "$tmpdir/macro-write.out")" != 'm--|13|19|set_pid|task_struct.pid' ]; then
	cat "$tmpdir/macro-write.out" >&2
	fail "macro argument write was not indexed"
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
