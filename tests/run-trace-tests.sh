#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

valid_trace()
{
	awk '
		!/^{"pid":[0-9]+,"command":"(compiler|index)","source":"[^"]+","phase":"[^"]+","start_ns":[0-9]+,"duration_ns":[0-9]+(,"items_in":[0-9]+,"items_out":[0-9]+)?}$/ {
			invalid = 1
		}
		END { exit invalid || NR == 0 }
	' "$1"
}

if [ -z "${SEMINDEX:-}" ] || [ -z "${COMPILE_COMMANDS:-}" ] || [ -z "${SOURCE_DIR:-}" ]; then
	fail "SEMINDEX, COMPILE_COMMANDS, and SOURCE_DIR must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
source=$SOURCE_DIR/tests/test.c
db=$tmpdir/semindex.db
commands_db=$tmpdir/commands.db
trace=$tmpdir/trace.jsonl

"$SEMINDEX" compiler --trace="$trace" --commands-database="$commands_db" --database="$db" -- cc "$source"
"$SEMINDEX" index --trace="$trace" --commands-database="$commands_db" --database="$db" \
	--compile-commands="$COMPILE_COMMANDS" "$source" >/dev/null

valid_trace "$trace" || fail "trace contains an incomplete or malformed line"
for phase in parse db.stage_records db.merge.begin db.merge.records_insert symbol_database command_database total; do
	if [ "$(grep -c "\"phase\":\"$phase\"" "$trace")" != 2 ]; then
		fail "trace does not contain two $phase events"
	fi
done
if ! grep -q '"command":"compiler"' "$trace" || ! grep -q '"command":"index"' "$trace"; then
	fail "trace omitted an indexing command"
fi
if [ "$(grep -c '"phase":"db.stage_records".*"items_in":[0-9][0-9]*,"items_out":[0-9][0-9]*' "$trace")" != 2 ] ||
	[ "$(grep -c '"phase":"db.merge.records_insert".*"items_in":[0-9][0-9]*,"items_out":[0-9][0-9]*' "$trace")" != 2 ]; then
	fail "record counters were not traced"
fi

printf '%s\n' 'struct traced_shared { int value; };' >"$tmpdir/shared.h"
printf '%s\n' '#include "shared.h"' \
	'int read_one(struct traced_shared *p) { return p->value; }' >"$tmpdir/one.c"
printf '%s\n' '#include "shared.h"' \
	'int read_two(struct traced_shared *p) { return p->value; }' >"$tmpdir/two.c"
flow_trace=$tmpdir/flow.jsonl
for input in "$tmpdir/one.c" "$tmpdir/two.c"; do
	"$SEMINDEX" compiler --trace="$flow_trace" --no-store-command --database="$tmpdir/flow.db" -- \
		cc -I"$tmpdir" "$input"
done
"$SOURCE_DIR/scripts/analyze-trace.sh" "$flow_trace" >"$tmpdir/flow.out"
if ! awk '
	/^  in-memory records: / { input = $3 }
	/^  private staging records: / { staged = $4 }
	END { exit !(staged < input) }
' "$tmpdir/flow.out"; then
	cat "$tmpdir/flow.out" >&2
	fail "record counters did not report the cached shared header"
fi
if ! awk '/^  reused files: / && $3 > 0 { found = 1 } END { exit !found }' "$tmpdir/flow.out"; then
	cat "$tmpdir/flow.out" >&2
	fail "file counters did not report a fingerprint cache hit"
fi

parallel_trace=$tmpdir/parallel.jsonl
pids=
for worker in 1 2 3 4 5 6 7 8; do
	"$SEMINDEX" compiler --trace="$parallel_trace" --no-store-command --variant="trace-$worker" \
		--database="$db" -- cc "$source" >"$tmpdir/worker-$worker.out" \
		2>"$tmpdir/worker-$worker.err" &
	pids="$pids $!"
done
for pid in $pids; do
	if ! wait "$pid"; then
		cat "$tmpdir"/worker-*.err >&2
		fail "parallel traced writer failed"
	fi
done

valid_trace "$parallel_trace" || fail "parallel writers interleaved trace lines"
if [ "$(grep -c '"phase":"total"' "$parallel_trace")" != 8 ]; then
	fail "parallel trace lost a process total"
fi
if [ "$(grep -c '"phase":"db.merge.begin"' "$parallel_trace")" != 8 ]; then
	fail "parallel trace lost a merge wait event"
fi

"$SOURCE_DIR/scripts/analyze-trace.sh" --limit=3 "$parallel_trace" >"$tmpdir/analysis.out"
if ! grep -q '^Processes: 8$' "$tmpdir/analysis.out" ||
		! grep -q '^db.merge.begin ' "$tmpdir/analysis.out" ||
		! grep -q '^Record flow:$' "$tmpdir/analysis.out" ||
		! grep -q '^File fingerprint cache:$' "$tmpdir/analysis.out" ||
	! grep -q '^Slowest translation units:$' "$tmpdir/analysis.out"; then
	cat "$tmpdir/analysis.out" >&2
	fail "trace analysis omitted expected results"
fi

printf '%s\n' '{broken' >"$tmpdir/malformed.jsonl"
if "$SOURCE_DIR/scripts/analyze-trace.sh" "$tmpdir/malformed.jsonl" \
	>"$tmpdir/malformed.out" 2>"$tmpdir/malformed.err"; then
	fail "malformed trace was accepted"
fi
if ! grep -q 'malformed line 1' "$tmpdir/malformed.err"; then
	cat "$tmpdir/malformed.err" >&2
	fail "malformed trace diagnostic differs"
fi

if "$SEMINDEX" compiler --trace="$tmpdir/missing/trace.jsonl" --no-store-command \
	--database="$db" -- cc "$source" >"$tmpdir/open.out" 2>"$tmpdir/open.err"; then
	fail "unwritable trace path succeeded"
fi
if ! grep -q 'failed to open trace' "$tmpdir/open.err"; then
	cat "$tmpdir/open.err" >&2
	fail "trace open error did not explain the failure"
fi
