#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

if [ -z "${SEMINDEX:-}" ] || [ -z "${SOURCE_DIR:-}" ]; then
	fail "SEMINDEX and SOURCE_DIR must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
symbols_db=$tmpdir/semindex.db
commands_db=$tmpdir/commands.db
output=$tmpdir/compile_commands.json

"$SEMINDEX" compiler --database "$symbols_db" --commands-database "$commands_db" -- \
	cc '-DJSON_VALUE="quoted value"' "$SOURCE_DIR/tests/test1.c"
"$SEMINDEX" compiler --database "$symbols_db" --commands-database "$commands_db" -- \
	cc "$SOURCE_DIR/tests/test.c"
"$SEMINDEX" compiler --variant=debug --database "$symbols_db" \
	--commands-database "$commands_db" -- cc -DDEBUG "$SOURCE_DIR/tests/test.c"

if ! "$SEMINDEX" compile-commands --database "$commands_db" --output "$output"; then
	fail "compile command export failed"
fi
if [ "$(sqlite3 :memory: "SELECT json_valid(CAST(readfile('$output') AS TEXT))")" != 1 ]; then
	fail "compile command export is not valid JSON"
fi
if [ "$(sqlite3 :memory: "SELECT json_array_length(CAST(readfile('$output') AS TEXT))")" != 2 ]; then
	fail "compile command export contains an unexpected number of entries"
fi
if [ "$(sqlite3 :memory: "SELECT value FROM json_each(CAST(readfile('$output') AS TEXT), '\$[1].arguments') WHERE key = 1")" != '-DJSON_VALUE="quoted value"' ]; then
	fail "compile command argument escaping differs"
fi
if [ "$(sqlite3 :memory: "SELECT json_extract(CAST(readfile('$output') AS TEXT), '\$[0].file')")" != "$SOURCE_DIR/tests/test.c" ]; then
	fail "compile commands are not ordered by source file"
fi

if ! "$SEMINDEX" compile-commands --database "$commands_db" --variant=debug >"$tmpdir/debug.json"; then
	fail "variant compile command export failed"
fi
if [ "$(sqlite3 :memory: "SELECT json_array_length(CAST(readfile('$tmpdir/debug.json') AS TEXT))")" != 1 ]; then
	fail "variant compile command export was not filtered"
fi
