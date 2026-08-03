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
source_path=tests/test11.c
callgraph_path=tests/callgraph-a.c

"$SEMINDEX" index --database="$db" --compile-commands="$COMPILE_COMMANDS" "$source" >/dev/null
"$SEMINDEX" compiler --database="$db" --no-store-command -- \
	cc --no-default-config "$callgraph_source"
"$SEMINDEX" compiler --database="$db" --variant=debug --no-store-command -- \
	cc --no-default-config "$source"
"$SEMINDEX" compiler --database="$db" --no-store-command -- \
	cc --no-default-config "$SOURCE_DIR/tests/test8.c"
"$DATABASE_API_TEST" "$db" "$source_path" "$callgraph_path" "$SOURCE_DIR"

if [ "$(sqlite3 "$db" "SELECT declared_type || ':' || canonical_type FROM symbol_types
WHERE symbol = 'c' AND kind = 0")" != "counter_t:int" ]; then
	fail "typedef relationship was not stored"
fi

if [ "$(sqlite3 "$db" "SELECT DISTINCT type_symbol || ':' || type_kind || ':' || (type_usr_id != 0)
FROM symbol_types WHERE symbol = 'Outer.inner' AND kind = 1")" != "Inner:2:1" ]; then
	fail "field type identity was not stored"
fi

position_plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN
SELECT records.symbol FROM files JOIN records ON records.file_id = files.id
WHERE files.path = '$source_path' AND files.variant = 'general'
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

cursor_plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN
SELECT files.path FROM records JOIN files ON files.id = records.file_id
WHERE records.symbol = 'Outer.y'
AND (files.variant, files.path, records.line, records.column, records.symbol,
records.record, records.action, records.kind, records.mode)
> ('general', '', 0, 0, '', 0, 0, 0, 0)
ORDER BY files.variant, files.path, records.line, records.column, records.symbol,
records.record, records.action, records.kind, records.mode LIMIT 3")
if ! printf '%s\n' "$cursor_plan" |
	grep -q 'SEARCH records USING PRIMARY KEY (symbol=?)'; then
	printf '%s\n' "$cursor_plan" >&2
	fail "paginated exact lookup does not use the records primary key"
fi
if printf '%s\n' "$cursor_plan" | grep -q 'SCAN records'; then
	printf '%s\n' "$cursor_plan" >&2
	fail "paginated exact lookup scans all records"
fi

field_id=$(sqlite3 "$db" "SELECT printf('%016x', usr_id) FROM records
WHERE symbol = 'Outer.y' AND record = 0 LIMIT 1")
if [ -z "$field_id" ]; then
	fail "field record does not have a stable identity"
fi

identity_plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN
SELECT files.path FROM records JOIN files ON files.id = records.file_id
WHERE records.symbol = 'Outer.y' AND records.usr_id = 0x$field_id
AND records.kind = 1 AND files.variant = 'general'")
if ! printf '%s\n' "$identity_plan" |
	grep -q 'SEARCH records USING PRIMARY KEY (symbol=?)'; then
	printf '%s\n' "$identity_plan" >&2
	fail "identity lookup does not use the records primary key"
fi

type_plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN
SELECT files.path FROM symbol_types JOIN files ON files.id = symbol_types.file_id
WHERE symbol_types.symbol = 'Outer.y' AND symbol_types.kind = 1
AND symbol_types.usr_id = 0x$field_id AND files.variant = 'general'")
if ! printf '%s\n' "$type_plan" |
	grep -q 'SEARCH symbol_types USING PRIMARY KEY (symbol=? AND kind=? AND usr_id=?)'; then
	printf '%s\n' "$type_plan" >&2
	fail "declared type lookup does not use the symbol_types primary key"
fi
if printf '%s\n' "$type_plan" | grep -q 'SCAN symbol_types'; then
	printf '%s\n' "$type_plan" >&2
	fail "declared type lookup scans all symbol types"
fi

function_id=$(sqlite3 "$db" "SELECT printf('%016x', usr_id) FROM records
WHERE symbol = 'indirect_a' AND record = 0 LIMIT 1")
function_type_plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN
SELECT files.path FROM function_types JOIN files ON files.id = function_types.file_id
WHERE function_types.symbol = 'indirect_a' AND function_types.usr_id = 0x$function_id
AND files.variant = 'general'")
if ! printf '%s\n' "$function_type_plan" |
	grep -q 'SEARCH function_types USING PRIMARY KEY (symbol=? AND usr_id=?)'; then
	printf '%s\n' "$function_type_plan" >&2
	fail "function type lookup does not use the primary key"
fi
if printf '%s\n' "$function_type_plan" | grep -q 'SCAN function_types'; then
	printf '%s\n' "$function_type_plan" >&2
	fail "function type lookup scans all function types"
fi
if printf '%s\n' "$identity_plan" | grep -q 'SCAN records'; then
	printf '%s\n' "$identity_plan" >&2
	fail "identity lookup scans all records"
fi

variant_plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN
SELECT name, repository_root, git_commit FROM variants ORDER BY name")
if ! printf '%s\n' "$variant_plan" | grep -q 'SCAN variants'; then
	printf '%s\n' "$variant_plan" >&2
	fail "variant listing does not use table order"
fi
if printf '%s\n' "$variant_plan" | grep -q 'USE TEMP B-TREE'; then
	printf '%s\n' "$variant_plan" >&2
	fail "variant listing requires temporary sorting"
fi

file_plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN
SELECT variant, path, mtime_ns, size FROM files
WHERE variant = 'general' AND path = '$source_path'")
if ! printf '%s\n' "$file_plan" |
	grep -q 'SEARCH files USING INDEX sqlite_autoindex_files_1 (variant=? AND path=?)'; then
	printf '%s\n' "$file_plan" >&2
	fail "file metadata lookup does not use the variant and path index"
fi
