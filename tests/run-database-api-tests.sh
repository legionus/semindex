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

"$SEMINDEX" index --database="$db" --compile-commands="$COMPILE_COMMANDS" "$source" >/dev/null
"$DATABASE_API_TEST" "$db" "$source"

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
