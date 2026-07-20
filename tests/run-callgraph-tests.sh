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
db=$tmpdir/semindex.db
commands_db=$tmpdir/commands.db
source_a=$SOURCE_DIR/tests/callgraph-a.c
source_b=$SOURCE_DIR/tests/callgraph-b.c

"$SEMINDEX" compiler --database="$db" --commands-database="$commands_db" -- cc "$source_a"
"$SEMINDEX" compiler --database="$db" --commands-database="$commands_db" -- cc "$source_b"

"$SEMINDEX" callgraph --database="$db" --variant=general --callees=caller >"$tmpdir/callees.out"
sed "s|$SOURCE_DIR|@SOURCE_DIR@|g" "$tmpdir/callees.out" >"$tmpdir/callees.normalized"
if ! cmp -s "$SOURCE_DIR/tests/callgraph.callees.expect" "$tmpdir/callees.normalized"; then
	diff -u "$SOURCE_DIR/tests/callgraph.callees.expect" "$tmpdir/callees.normalized" >&2 || true
	fail "callee output differs"
fi

"$SEMINDEX" callgraph --database="$db" --variant=general --callers=leaf >"$tmpdir/callers.out"
sed "s|$SOURCE_DIR|@SOURCE_DIR@|g" "$tmpdir/callers.out" >"$tmpdir/callers.normalized"
if ! cmp -s "$SOURCE_DIR/tests/callgraph.callers.expect" "$tmpdir/callers.normalized"; then
	diff -u "$SOURCE_DIR/tests/callgraph.callers.expect" "$tmpdir/callers.normalized" >&2 || true
	fail "caller output differs"
fi

caller_id=$("$SEMINDEX" callgraph --database="$db" --callees=caller --path='*/callgraph-a.c' --show-id |
	awk -F '\t' 'NR == 1 { print $2 }')
if [ -z "$caller_id" ]; then
	fail "caller ID was not stored"
fi
"$SEMINDEX" callgraph --database="$db" --callees=caller --id="$caller_id" >"$tmpdir/id.out"
if [ "$(wc -l <"$tmpdir/id.out")" != 4 ] || grep -q 'callgraph-b.c' "$tmpdir/id.out"; then
	cat "$tmpdir/id.out" >&2
	fail "caller ID did not disambiguate static functions"
fi

"$SEMINDEX" callgraph --database="$db" --callees=caller --id="$caller_id" --show-id \
	>"$tmpdir/show-id.out"
if awk -F '\t' 'NF != 5 || $2 == "" || $4 == "" { found = 1 } END { exit found }' \
	"$tmpdir/show-id.out"; then
	:
else
	cat "$tmpdir/show-id.out" >&2
	fail "--show-id omitted a stable function identity"
fi

"$SEMINDEX" callgraph --database="$db" --callees=caller --path='*/callgraph-b.c' >"$tmpdir/path.out"
if [ "$(wc -l <"$tmpdir/path.out")" != 1 ] || grep -qv 'callgraph-b.c' "$tmpdir/path.out"; then
	cat "$tmpdir/path.out" >&2
	fail "callsite path filter returned unexpected results"
fi

"$SEMINDEX" compiler --variant=debug --database="$db" --commands-database="$commands_db" -- cc "$source_a"
"$SEMINDEX" callgraph --database="$db" --variant=debug --callees=caller >"$tmpdir/variant.out"
if [ "$(wc -l <"$tmpdir/variant.out")" != 4 ] || grep -qv "debug:$source_a:" "$tmpdir/variant.out"; then
	cat "$tmpdir/variant.out" >&2
	fail "variant filter returned unexpected results"
fi

if [ "$(sqlite3 "$db" 'SELECT count(*) FROM records WHERE action != 3 AND (usr_id IS NOT NULL OR context_usr_id IS NOT NULL)')" != 0 ]; then
	fail "function IDs were stored for non-call records"
fi
if [ "$(sqlite3 "$db" 'SELECT count(*) FROM records WHERE action = 3 AND kind = 7 AND (usr_id IS NULL OR context_usr_id IS NULL)')" != 0 ]; then
	fail "direct call record lacks a stable function identity"
fi
if [ "$(sqlite3 "$db" 'SELECT count(*) FROM records WHERE action = 3 AND kind != 7 AND (usr_id IS NOT NULL OR context_usr_id IS NOT NULL)')" != 0 ]; then
	fail "function IDs were stored for an indirect call"
fi

plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN SELECT symbol FROM records WHERE record = 1 AND action = 3 AND kind = 7 AND context = 'caller'")
if ! printf '%s\n' "$plan" | grep -q 'records_call_context_idx'; then
	printf '%s\n' "$plan" >&2
	fail "callee lookup does not use the partial caller index"
fi
plan=$(sqlite3 "$db" "EXPLAIN QUERY PLAN SELECT context FROM records WHERE record = 1 AND action = 3 AND kind = 7 AND symbol = 'leaf'")
if ! printf '%s\n' "$plan" | grep -q 'USING PRIMARY KEY (symbol=?'; then
	printf '%s\n' "$plan" >&2
	fail "caller lookup does not use the records primary key"
fi

if "$SEMINDEX" callgraph --database="$db" --callers=leaf --callees=caller \
	>"$tmpdir/invalid.out" 2>"$tmpdir/invalid.err"; then
	fail "conflicting callgraph directions succeeded"
fi
if ! grep -q 'specify exactly one' "$tmpdir/invalid.err"; then
	cat "$tmpdir/invalid.err" >&2
	fail "conflicting direction diagnostic differs"
fi

if "$SEMINDEX" callgraph --database="$db" --callees=caller --id=-1 \
	>"$tmpdir/invalid-id.out" 2>"$tmpdir/invalid-id.err"; then
	fail "invalid function ID succeeded"
fi
if ! grep -q 'invalid function ID' "$tmpdir/invalid-id.err"; then
	cat "$tmpdir/invalid-id.err" >&2
	fail "invalid function ID diagnostic differs"
fi
