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
explicit_commit=1111111111111111111111111111111111111111
git_commit_option=

if "$SEMINDEX" compiler --help | grep -q -- '--git-commit'; then
git_commit_option=--git-commit=$explicit_commit
git_repo=$tmpdir/git-repository
git_db=$tmpdir/git-repository.db
mkdir -p "$git_repo"
git -C "$git_repo" init -q
git -C "$git_repo" config user.email semindex@example.invalid
git -C "$git_repo" config user.name semindex
printf '%s\n' 'int git_repository_symbol;' >"$git_repo/main.c"
git -C "$git_repo" add main.c
git -C "$git_repo" commit -qm initial
git_commit=$(git -C "$git_repo" rev-parse HEAD)
"$SEMINDEX" compiler --database "$git_db" --no-store-command --git-commit=auto -- \
	cc --no-default-config "$git_repo/main.c"

if [ "$(sqlite3 "$git_db" "SELECT git_commit FROM variants WHERE name = 'general'")" != "$git_commit" ]; then
	fail "compiler did not store the repository commit"
fi

if [ "$(sqlite3 "$git_db" "SELECT repository_root FROM variants WHERE name = 'general'")" != "$git_repo" ]; then
	fail "compiler did not store the repository root"
fi

git -C "$git_repo" pack-refs --all --prune
"$SEMINDEX" compiler --database "$git_db" --no-store-command --git-commit=auto --variant=packed -- \
	cc --no-default-config "$git_repo/main.c"

if [ "$(sqlite3 "$git_db" "SELECT git_commit FROM variants WHERE name = 'packed'")" != "$git_commit" ]; then
	fail "compiler did not resolve a packed Git reference"
fi

linked_repo=$tmpdir/linked-repository
git -C "$git_repo" worktree add -q -b linked "$linked_repo"
"$SEMINDEX" compiler --database "$git_db" --no-store-command --git-commit=auto --variant=linked -- \
	cc --no-default-config "$linked_repo/main.c"

if [ "$(sqlite3 "$git_db" "SELECT git_commit FROM variants WHERE name = 'linked'")" != "$git_commit" ]; then
	fail "compiler did not resolve a linked worktree reference"
fi

explicit_db=$tmpdir/explicit.db
compile_commands_dir=$tmpdir/explicit-commands
compile_commands=$compile_commands_dir/compile_commands.json
mkdir -p "$compile_commands_dir"
printf '%s\n' 'int explicit_commit_symbol;' >"$tmpdir/explicit.c"
printf '[{"directory":"%s","file":"%s","arguments":["cc","--no-default-config","%s"]}]\n' \
	"$tmpdir" "$tmpdir/explicit.c" "$tmpdir/explicit.c" >"$compile_commands"
"$SEMINDEX" index --database "$explicit_db" --no-store-command \
	--git-commit="$explicit_commit" --compile-commands="$compile_commands_dir" \
	"$tmpdir/explicit.c" >/dev/null

if [ "$(sqlite3 "$explicit_db" "SELECT git_commit FROM variants WHERE name = 'general'")" != "$explicit_commit" ]; then
	fail "index did not store an explicit commit"
fi

if [ "$(sqlite3 "$explicit_db" "SELECT repository_root IS NULL FROM variants WHERE name = 'general'")" != 1 ]; then
	fail "explicit commit outside a repository acquired a repository root"
fi

disabled_db=$tmpdir/disabled.db
"$SEMINDEX" compiler --database "$disabled_db" --no-store-command --no-git-commit -- \
	cc --no-default-config "$git_repo/main.c"

if [ "$(sqlite3 "$disabled_db" "SELECT COUNT(*) FROM variants WHERE name = 'general' AND git_commit IS NULL AND repository_root = '$git_repo'")" != 1 ]; then
	fail "variant metadata without Git provenance was not stored"
fi

if "$SEMINDEX" compiler --git-commit=invalid --no-store-command -- \
	cc --no-default-config "$git_repo/main.c" >"$tmpdir/invalid.out" 2>"$tmpdir/invalid.err"; then
	fail "invalid Git commit was accepted"
fi

if ! grep -q 'invalid Git commit' "$tmpdir/invalid.err"; then
	fail "invalid Git commit error was not reported"
fi
fi

repo=$tmpdir/repository
repo_db=$tmpdir/repository.db
mkdir -p "$repo/src" "$repo/include"
git -C "$repo" init -q
git -C "$repo" config user.email semindex@example.invalid
git -C "$repo" config user.name semindex
printf '%s\n' 'struct repository_local { int member; };' >"$repo/include/local.h"
printf '%s\n' 'struct repository_external { int member; };' >"$tmpdir/external.h"
printf '%s\n' '#include "local.h"' '#include "external.h"' \
	'int repository_read(struct repository_local *local, struct repository_external *external)' \
	'{' 'return local->member + external->member;' '}' >"$repo/src/main.c"
git -C "$repo" add .
git -C "$repo" commit -qm initial
"$SEMINDEX" compiler --database "$repo_db" --no-store-command -- \
	cc --no-default-config -I"$repo/include" -I"$tmpdir" "$repo/src/main.c"
if [ "$(sqlite3 "$repo_db" "SELECT COUNT(*) FROM files WHERE path = 'src/main.c'")" != 1 ]; then
	fail "repository source path was not stored relative to the Git root"
fi
if [ "$(sqlite3 "$repo_db" "SELECT COUNT(*) FROM files WHERE path = 'include/local.h'")" != 1 ]; then
	fail "repository header path was not stored relative to the Git root"
fi
if [ "$(sqlite3 "$repo_db" "SELECT COUNT(*) FROM files WHERE path = '$tmpdir/external.h'")" != 1 ]; then
	fail "header outside the repository was stored as a relative path"
fi

sqlite3 "$repo_db" "UPDATE variants SET git_commit = '$explicit_commit' WHERE name = 'general'"
"$SEMINDEX" compiler --database "$repo_db" --no-store-command -- \
	cc --no-default-config -I"$repo/include" -I"$tmpdir" "$repo/src/main.c"

if [ "$(sqlite3 "$repo_db" "SELECT git_commit FROM variants WHERE name = 'general'")" != "$explicit_commit" ]; then
	fail "indexing without provenance discarded the recorded commit"
fi

printf '%s\n' 'struct shared { int pid; int other; };' >"$tmpdir/shared.h"

pids=
for worker in 1 2 3 4 5 6 7 8; do
	printf '%s\n' '#include "shared.h"' \
		"int read_$worker(struct shared *p) { return p->pid; }" >"$tmpdir/worker-$worker.c"
	"$SEMINDEX" compiler --database "$db" $git_commit_option -- cc --no-default-config \
		-I"$tmpdir" "$tmpdir/worker-$worker.c" \
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
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM symbol_types JOIN files ON files.id = symbol_types.file_id WHERE symbol_types.symbol = 'shared.pid' AND files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "shared header declared type was duplicated"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM file_fingerprints JOIN files ON files.id = file_fingerprints.file_id WHERE files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "parallel writers duplicated the shared header fingerprint"
fi
if [ -n "$git_commit_option" ] &&
	[ "$(sqlite3 "$db" "SELECT COUNT(*) FROM variants WHERE name = 'general' AND git_commit = '$explicit_commit'")" != 1 ]; then
	fail "parallel writers lost or duplicated variant provenance"
fi

printf '%s\n' '#include "shared.h"' \
	'int read_9(struct shared *p) { return p->pid; }' >"$tmpdir/worker-9.c"
"$SEMINDEX" compiler --database "$db" -- cc --no-default-config \
	-I"$tmpdir" "$tmpdir/worker-9.c"
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM file_fingerprints JOIN files ON files.id = file_fingerprints.file_id WHERE files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "reusing a header added a duplicate fingerprint"
fi
if [ -n "$git_commit_option" ] &&
	[ "$(sqlite3 "$db" "SELECT git_commit FROM variants WHERE name = 'general'")" != "$explicit_commit" ]; then
	fail "indexing without provenance discarded the recorded commit"
fi

printf '%s\n' '#include "shared.h"' \
	'long read_1(struct shared *p, int extra) { return p->other + extra; }' >"$tmpdir/worker-1.c"
"$SEMINDEX" compiler --database "$db" -- cc --no-default-config \
	-I"$tmpdir" "$tmpdir/worker-1.c"
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM function_types JOIN files ON files.id = function_types.file_id
WHERE function_types.symbol = 'read_1' AND files.path = '$tmpdir/worker-1.c'")" != 3 ]; then
	fail "reindexing a function retained stale signature rows"
fi
if [ "$(sqlite3 "$db" "SELECT declared_type FROM function_types JOIN files ON files.id = function_types.file_id
WHERE function_types.symbol = 'read_1' AND function_types.position = -1
AND files.path = '$tmpdir/worker-1.c'")" != long ]; then
	fail "reindexing a function retained its old return type"
fi

if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE symbol = 'shared.pid'")" != 9 ]; then
	fail "reindexing one source damaged records from other sources"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE symbol = 'shared.other'")" != 2 ]; then
	fail "reindexed source did not replace its old records"
fi

variant_db=$tmpdir/variant.db
printf '%s\n' '#include "shared.h"' \
	'int variant_read(struct shared *p) { return p->pid; }' >"$tmpdir/variant.c"
"$SEMINDEX" compiler --database "$variant_db" -- cc --no-default-config \
	-I"$tmpdir" "$tmpdir/variant.c"
"$SEMINDEX" compiler --variant=debug --database "$variant_db" -- \
	cc --no-default-config -I"$tmpdir" "$tmpdir/variant.c"

if [ "$(sqlite3 "$variant_db" "SELECT COUNT(DISTINCT variant) FROM files WHERE path = '$tmpdir/variant.c'")" != 2 ]; then
	fail "source file variants were not stored separately"
fi

printf '%s\n' '#include "shared.h"' \
	'int variant_read(struct shared *p) { return p->other; }' >"$tmpdir/variant.c"
"$SEMINDEX" compiler --database "$variant_db" -- cc --no-default-config \
	-I"$tmpdir" "$tmpdir/variant.c"

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
"$SEMINDEX" compiler --database "$context_db" -- cc --no-default-config \
	-I"$tmpdir" "$tmpdir/context-a.c"
"$SEMINDEX" compiler --database "$context_db" -- cc --no-default-config \
	-DWITH_EXTRA -I"$tmpdir" "$tmpdir/context-b.c"
if [ "$(sqlite3 "$context_db" "SELECT COUNT(*) FROM file_fingerprints JOIN files ON files.id = file_fingerprints.file_id WHERE files.path = '$tmpdir/conditional.h'")" != 2 ]; then
	fail "different macro contexts shared a header fingerprint"
fi
if [ "$(sqlite3 "$context_db" "SELECT COUNT(*) FROM records WHERE symbol = 'conditional.extra'")" != 2 ]; then
	fail "a distinct header context was omitted from the index"
fi

type_context_db=$tmpdir/type-context.db
printf '%s\n' '#ifdef WIDE_VALUE' '#define VALUE_TYPE long' '#else' '#define VALUE_TYPE int' '#endif' \
	'struct typed_context { VALUE_TYPE value; };' >"$tmpdir/type-context.h"
printf '%s\n' '#include "type-context.h"' \
	'int read_narrow(struct typed_context *p) { return p->value; }' >"$tmpdir/type-context-a.c"
printf '%s\n' '#include "type-context.h"' \
	'long read_wide(struct typed_context *p) { return p->value; }' >"$tmpdir/type-context-b.c"
"$SEMINDEX" compiler --database "$type_context_db" --no-store-command -- \
	cc --no-default-config -I"$tmpdir" "$tmpdir/type-context-a.c"
"$SEMINDEX" compiler --database "$type_context_db" --no-store-command -- \
	cc --no-default-config -DWIDE_VALUE -I"$tmpdir" "$tmpdir/type-context-b.c"

if [ "$(sqlite3 "$type_context_db" "SELECT COUNT(DISTINCT declared_type) FROM symbol_types WHERE symbol = 'typed_context.value'")" != 2 ]; then
	fail "different type contexts shared a header fingerprint"
fi

local_db=$tmpdir/local.db
printf '%s\n' 'static inline int local_header(void)' '{' 'int hidden = 1;' 'return hidden;' '}' \
	>"$tmpdir/local.h"
printf '%s\n' '#include "local.h"' 'int call_local_a(void) { return local_header(); }' >"$tmpdir/local-a.c"
printf '%s\n' '#include "local.h"' 'int call_local_b(void) { return local_header(); }' >"$tmpdir/local-b.c"
"$SEMINDEX" compiler --no-include-local --database "$local_db" -- \
	cc --no-default-config -I"$tmpdir" "$tmpdir/local-a.c"
"$SEMINDEX" compiler --database "$local_db" -- cc --no-default-config \
	-I"$tmpdir" "$tmpdir/local-b.c"
if [ "$(sqlite3 "$local_db" "SELECT COUNT(*) FROM records WHERE symbol = 'hidden' AND local = 1")" = 0 ]; then
	fail "a non-local fingerprint hid local header records"
fi
if [ "$(sqlite3 "$local_db" "SELECT COUNT(*) FROM file_fingerprints JOIN files ON files.id = file_fingerprints.file_id WHERE files.path = '$tmpdir/local.h'")" != 2 ]; then
	fail "local and non-local header fingerprints were not distinguished"
fi

printf '%s\n' 'struct shared { int replacement; };' >"$tmpdir/shared.h"
printf '%s\n' '#include "shared.h"' \
	'int read_new(struct shared *p) { return p->replacement; }' >"$tmpdir/worker-new.c"
"$SEMINDEX" compiler --database "$db" -- cc --no-default-config \
	-I"$tmpdir" "$tmpdir/worker-new.c"
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records JOIN files ON files.id = records.file_id WHERE records.symbol = 'shared.pid' AND files.path = '$tmpdir/shared.h'")" != 0 ]; then
	fail "changing a header retained records from an old fingerprint"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM symbol_types JOIN files ON files.id = symbol_types.file_id WHERE symbol_types.symbol = 'shared.pid' AND files.path = '$tmpdir/shared.h'")" != 0 ]; then
	fail "changing a header retained an old declared type"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records JOIN files ON files.id = records.file_id WHERE records.symbol = 'shared.replacement' AND files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "changing a header did not store its new records"
fi
if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM file_fingerprints JOIN files ON files.id = file_fingerprints.file_id WHERE files.path = '$tmpdir/shared.h'")" != 1 ]; then
	fail "changing a header retained obsolete fingerprints"
fi

last_clean_source=$tmpdir/last-clean.c
last_clean_db=$tmpdir/last-clean-compiler.db
printf '%s\n' 'int compiler_last_clean(void) { return 1; }' >"$last_clean_source"
"$SEMINDEX" compiler --no-store-command --database "$last_clean_db" -- \
	cc --no-default-config "$last_clean_source"
printf '%s\n' 'int compiler_partial_before;' 'int compiler_broken(void)' '{' \
	'int value = ;' '}' 'int compiler_partial_after;' >"$last_clean_source"
if ! "$SEMINDEX" compiler --no-store-command --database "$last_clean_db" -- \
	cc --no-default-config "$last_clean_source" >"$tmpdir/partial.out" 2>"$tmpdir/partial.err"; then
	fail "compiler rejected a partial index"
fi
if [ "$(sqlite3 "$last_clean_db" "SELECT COUNT(*) FROM records WHERE symbol = 'compiler_last_clean'")" != 0 ]; then
	fail "compiler retained a stale clean index after a partial run"
fi
if [ "$(sqlite3 "$last_clean_db" "SELECT COUNT(*) FROM records WHERE symbol GLOB 'compiler_partial_*'")" != 2 ]; then
	fail "compiler omitted records from a partial run"
fi

last_clean_db=$tmpdir/last-clean-index.db
compile_commands=$tmpdir/compile_commands.json
printf '%s\n' 'int index_last_clean(void) { return 1; }' >"$last_clean_source"
printf '[{"directory":"%s","file":"%s","arguments":["cc","--no-default-config","%s"]}]\n' \
	"$tmpdir" "$last_clean_source" "$last_clean_source" >"$compile_commands"
"$SEMINDEX" index --no-store-command --database "$last_clean_db" \
	--compile-commands "$compile_commands" "$last_clean_source" >/dev/null
printf '%s\n' 'int index_partial_before;' 'int index_broken(void)' '{' \
	'int value = ;' '}' 'int index_partial_after;' >"$last_clean_source"
if ! "$SEMINDEX" index --no-store-command --database "$last_clean_db" \
	--compile-commands "$compile_commands" "$last_clean_source" \
	>"$tmpdir/partial-index.out" 2>"$tmpdir/partial-index.err"; then
	fail "index rejected a partial index"
fi
if [ "$(sqlite3 "$last_clean_db" "SELECT COUNT(*) FROM records WHERE symbol = 'index_last_clean'")" != 0 ]; then
	fail "index retained a stale clean index after a partial run"
fi
if [ "$(sqlite3 "$last_clean_db" "SELECT COUNT(*) FROM records WHERE symbol GLOB 'index_partial_*'")" != 2 ]; then
	fail "index omitted records from a partial run"
fi

old_db=$tmpdir/old.db
sqlite3 "$old_db" 'CREATE TABLE old_records(value TEXT)'
if "$SEMINDEX" compiler --database "$old_db" -- cc --no-default-config \
	-I"$tmpdir" "$tmpdir/worker-new.c" \
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
