#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

if [ -z "${SEMINDEX:-}" ]; then
	fail "SEMINDEX must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
db=$tmpdir/.semindex/semindex.db

printf '%s\n' 'struct shared { int pid; int other; };' >"$tmpdir/shared.h"

pids=
for worker in 1 2 3 4 5 6 7 8; do
	printf '%s\n' '#include "shared.h"' \
		"int read_$worker(struct shared *p) { return p->pid; }" >"$tmpdir/worker-$worker.c"
	"$SEMINDEX" compiler --database "$db" -- cc -I"$tmpdir" "$tmpdir/worker-$worker.c" \
		>"$tmpdir/worker-$worker.out" 2>"$tmpdir/worker-$worker.err" &
	pids="$pids $!"
done
for pid in $pids; do
	if ! wait "$pid"; then
		cat "$tmpdir"/worker-*.err >&2
		fail "parallel database writer failed"
	fi
done

if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE symbol = 'shared.pid'")" != 9 ]; then
	fail "parallel merge lost or duplicated field records"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records JOIN files ON files.id = records.file_id WHERE records.symbol = 'shared.pid' AND files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "shared header record was duplicated"
fi

printf '%s\n' '#include "shared.h"' \
	'int read_1(struct shared *p) { return p->other; }' >"$tmpdir/worker-1.c"
"$SEMINDEX" compiler --database "$db" -- cc -I"$tmpdir" "$tmpdir/worker-1.c"

if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE symbol = 'shared.pid'")" != 8 ]; then
	fail "reindexing one source damaged records from other sources"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE symbol = 'shared.other'")" != 2 ]; then
	fail "reindexed source did not replace its old records"
fi

old_db=$tmpdir/old.db
sqlite3 "$old_db" 'CREATE TABLE old_records(value TEXT)'
if "$SEMINDEX" compiler --database "$old_db" -- cc "$tmpdir/worker-1.c" \
	>"$tmpdir/old.out" 2>"$tmpdir/old.err"; then
	fail "database with an unversioned old schema was accepted"
fi
if ! grep -q 'database schema is incompatible' "$tmpdir/old.err"; then
	cat "$tmpdir/old.err" >&2
	fail "old database error did not explain the incompatibility"
fi
if [ "$(sqlite3 "$old_db" "SELECT COUNT(*) FROM sqlite_schema WHERE name = 'old_records'")" != 1 ]; then
	fail "old database was modified"
fi
