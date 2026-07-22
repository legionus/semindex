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

commands_db=$tmpdir/.semindex/commands.db
if [ "$(sqlite3 "$commands_db" "SELECT COUNT(*) FROM commands")" != 8 ]; then
	fail "parallel command database writers lost records"
fi

if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE symbol = 'shared.pid'")" != 9 ]; then
	fail "parallel merge lost or duplicated field records"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records JOIN files ON files.id = records.file_id WHERE records.symbol = 'shared.pid' AND files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "shared header record was duplicated"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM file_fingerprints JOIN files ON files.id = file_fingerprints.file_id WHERE files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "parallel writers duplicated the shared header fingerprint"
fi

printf '%s\n' '#include "shared.h"' \
	'int read_9(struct shared *p) { return p->pid; }' >"$tmpdir/worker-9.c"
"$SEMINDEX" compiler --database "$db" -- cc -I"$tmpdir" "$tmpdir/worker-9.c"
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM file_fingerprints JOIN files ON files.id = file_fingerprints.file_id WHERE files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "reusing a header added a duplicate fingerprint"
fi

printf '%s\n' '#include "shared.h"' \
	'int read_1(struct shared *p) { return p->other; }' >"$tmpdir/worker-1.c"
"$SEMINDEX" compiler --database "$db" -- cc -I"$tmpdir" "$tmpdir/worker-1.c"

if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE symbol = 'shared.pid'")" != 9 ]; then
	fail "reindexing one source damaged records from other sources"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE symbol = 'shared.other'")" != 2 ]; then
	fail "reindexed source did not replace its old records"
fi

variant_db=$tmpdir/variant.db
printf '%s\n' '#include "shared.h"' \
	'int variant_read(struct shared *p) { return p->pid; }' >"$tmpdir/variant.c"
"$SEMINDEX" compiler --database "$variant_db" -- cc -I"$tmpdir" "$tmpdir/variant.c"
"$SEMINDEX" compiler --variant=debug --database "$variant_db" -- \
	cc -I"$tmpdir" "$tmpdir/variant.c"

if [ "$(sqlite3 "$variant_db" "SELECT COUNT(DISTINCT variant) FROM files WHERE path = '$tmpdir/variant.c'")" != 2 ]; then
	fail "source file variants were not stored separately"
fi

printf '%s\n' '#include "shared.h"' \
	'int variant_read(struct shared *p) { return p->other; }' >"$tmpdir/variant.c"
"$SEMINDEX" compiler --database "$variant_db" -- cc -I"$tmpdir" "$tmpdir/variant.c"

if [ "$(sqlite3 "$variant_db" "SELECT COUNT(*) FROM records JOIN files ON files.id = records.file_id WHERE records.symbol = 'shared.pid' AND files.variant = 'general'")" != 1 ]; then
	fail "reindexing did not replace records in the default variant"
fi
if [ "$(sqlite3 "$variant_db" "SELECT COUNT(*) FROM records JOIN files ON files.id = records.file_id WHERE records.symbol = 'shared.pid' AND files.variant = 'debug'")" != 2 ]; then
	fail "reindexing the default variant damaged another variant"
fi

context_db=$tmpdir/context.db
printf '%s\n' 'struct conditional {' 'int always;' '#ifdef WITH_EXTRA' 'int extra;' '#endif' '};' \
	>"$tmpdir/conditional.h"
printf '%s\n' '#include "conditional.h"' \
	'int read_always(struct conditional *p) { return p->always; }' >"$tmpdir/context-a.c"
printf '%s\n' '#include "conditional.h"' \
	'int read_extra(struct conditional *p) { return p->extra; }' >"$tmpdir/context-b.c"
"$SEMINDEX" compiler --database "$context_db" -- cc -I"$tmpdir" "$tmpdir/context-a.c"
"$SEMINDEX" compiler --database "$context_db" -- cc -DWITH_EXTRA -I"$tmpdir" "$tmpdir/context-b.c"
if [ "$(sqlite3 "$context_db" "SELECT COUNT(*) FROM file_fingerprints JOIN files ON files.id = file_fingerprints.file_id WHERE files.path = '$tmpdir/conditional.h'")" != 2 ]; then
	fail "different macro contexts shared a header fingerprint"
fi
if [ "$(sqlite3 "$context_db" "SELECT COUNT(*) FROM records WHERE symbol = 'conditional.extra'")" != 2 ]; then
	fail "a distinct header context was omitted from the index"
fi

local_db=$tmpdir/local.db
printf '%s\n' 'static inline int local_header(void)' '{' 'int hidden = 1;' 'return hidden;' '}' \
	>"$tmpdir/local.h"
printf '%s\n' '#include "local.h"' 'int call_local_a(void) { return local_header(); }' >"$tmpdir/local-a.c"
printf '%s\n' '#include "local.h"' 'int call_local_b(void) { return local_header(); }' >"$tmpdir/local-b.c"
"$SEMINDEX" compiler --no-include-local --database "$local_db" -- cc -I"$tmpdir" "$tmpdir/local-a.c"
"$SEMINDEX" compiler --database "$local_db" -- cc -I"$tmpdir" "$tmpdir/local-b.c"
if [ "$(sqlite3 "$local_db" "SELECT COUNT(*) FROM records WHERE symbol = 'hidden' AND local = 1")" = 0 ]; then
	fail "a non-local fingerprint hid local header records"
fi
if [ "$(sqlite3 "$local_db" "SELECT COUNT(*) FROM file_fingerprints JOIN files ON files.id = file_fingerprints.file_id WHERE files.path = '$tmpdir/local.h'")" != 2 ]; then
	fail "local and non-local header fingerprints were not distinguished"
fi

printf '%s\n' 'struct shared { int replacement; };' >"$tmpdir/shared.h"
printf '%s\n' '#include "shared.h"' \
	'int read_new(struct shared *p) { return p->replacement; }' >"$tmpdir/worker-new.c"
"$SEMINDEX" compiler --database "$db" -- cc -I"$tmpdir" "$tmpdir/worker-new.c"
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records JOIN files ON files.id = records.file_id WHERE records.symbol = 'shared.pid' AND files.path = '$tmpdir/shared.h'")" != 0 ]; then
	fail "changing a header retained records from an old fingerprint"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records JOIN files ON files.id = records.file_id WHERE records.symbol = 'shared.replacement' AND files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "changing a header did not store its new records"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM file_fingerprints JOIN files ON files.id = file_fingerprints.file_id WHERE files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "changing a header retained obsolete fingerprints"
fi

old_db=$tmpdir/old.db
sqlite3 "$old_db" 'CREATE TABLE old_records(value TEXT)'
if "$SEMINDEX" compiler --database "$old_db" -- cc -I"$tmpdir" "$tmpdir/worker-new.c" \
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
