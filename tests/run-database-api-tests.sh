#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

if [ -z "${SEMINDEX:-}" ] || [ -z "${DATABASE_API_TEST:-}" ] ||
   [ -z "${COMPILE_COMMANDS:-}" ] || [ -z "${SOURCE_DIR:-}" ]; then
	fail "SEMINDEX, DATABASE_API_TEST, COMPILE_COMMANDS, and SOURCE_DIR must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
db=$tmpdir/semindex.db
source=$SOURCE_DIR/tests/test11.c
callgraph_source=$SOURCE_DIR/tests/callgraph-a.c

"$SEMINDEX" index --database="$db" --compile-commands="$COMPILE_COMMANDS" "$source" >/dev/null
"$SEMINDEX" compiler --database="$db" --no-store-command -- cc "$callgraph_source"
"$DATABASE_API_TEST" "$db" "$source" "$callgraph_source"

position_plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN
SELECT records.symbol FROM files JOIN records ON records.file_id = files.id
WHERE files.path = '$source' AND files.variant = 'general'
AND records.line = 6 AND records.column <= 10")
if ! printf '%s\n' "$position_plan" |
	grep -q 'SEARCH records USING.*records_file_idx (file_id=?)'; then
	printf '%s\n' "$position_plan" >&2
	fail "position lookup does not use the file index"
fi
if printf '%s\n' "$position_plan" | grep -q 'SCAN records'; then
	printf '%s\n' "$position_plan" >&2
	fail "position lookup scans all records"
fi

symbol_plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN
SELECT files.path FROM records JOIN files ON files.id = records.file_id
WHERE records.symbol = 'Outer.y' AND records.record = 0
AND records.context = '' AND records.local = 0")
if ! printf '%s\n' "$symbol_plan" |
	grep -q 'SEARCH records USING PRIMARY KEY (symbol=? AND record=?)'; then
	printf '%s\n' "$symbol_plan" >&2
	fail "filtered symbol lookup does not use the records primary key"
fi
