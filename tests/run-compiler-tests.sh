#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

run_quiet_case()
{
	db=$tmpdir/quiet/.semindex/semindex.db
	commands_db=$tmpdir/quiet/.semindex/commands.db
	out=$tmpdir/quiet.out
	err=$tmpdir/quiet.err

	if ! "$SEMINDEX" compiler --database "$db" -- cc -I"$SOURCE_DIR/tests/include" \
	     -c "$SOURCE_DIR/tests/test.c" -o "$tmpdir/quiet.o" >"$out" 2>"$err"; then
		cat "$err" >&2
		fail "quiet compiler command failed"
	fi
	if [ -s "$out" ]; then
		cat "$out" >&2
		fail "compiler wrote output without --format"
	fi
	if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records")" -lt 1 ]; then
		fail "compiler did not store index records"
	fi
	if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE local != 0")" != 0 ]; then
		fail "compiler stored local records by default"
	fi
	if sqlite3 "$db" "SELECT 1 FROM compile_commands" >/dev/null 2>&1; then
		fail "compiler command storage is still enabled"
	fi
	if [ "$(sqlite3 "$commands_db" "SELECT COUNT(*) FROM commands")" != 1 ]; then
		fail "compiler command was not stored separately"
	fi
	if [ "$(sqlite3 "$commands_db" "SELECT variant FROM commands")" != general ]; then
		fail "compiler command used an unexpected variant"
	fi
	if [ "$(sqlite3 "$commands_db" "SELECT substr(hex(arguments), 1, 6) FROM commands")" != 636300 ]; then
		fail "compiler arguments are not stored as a NUL-separated blob"
	fi
}

run_no_store_command_case()
{
	db=$tmpdir/no-command/.semindex/semindex.db
	commands_db=$tmpdir/no-command/.semindex/commands.db

	if ! "$SEMINDEX" compiler --no-store-command --database "$db" -- \
	     "$SOURCE_DIR/tests/test.c" >/dev/null; then
		fail "compiler --no-store-command failed"
	fi
	if [ -e "$commands_db" ]; then
		fail "compiler --no-store-command created a command database"
	fi
}

run_index_command_case()
{
	db=$tmpdir/index/.semindex/semindex.db
	commands_db=$tmpdir/index/.semindex/commands.db

	if ! "$SEMINDEX" index --database "$db" --compile-commands "$COMPILE_COMMANDS" \
	     --variant=index-variant "$SOURCE_DIR/tests/test.c" >/dev/null; then
		fail "index command failed"
	fi
	if [ "$(sqlite3 "$commands_db" "SELECT COUNT(*) FROM commands WHERE variant = 'index-variant'")" != 1 ]; then
		fail "index did not store its selected compile command"
	fi
	if [ "$(sqlite3 "$commands_db" "SELECT instr(hex(arguments), hex('test.c')) FROM commands")" = 0 ]; then
		fail "index stored an unexpected compile command"
	fi

	db=$tmpdir/index-no-command/.semindex/semindex.db
	commands_db=$tmpdir/index-no-command/.semindex/commands.db
	if ! "$SEMINDEX" index --no-store-command --database "$db" \
	     --compile-commands "$COMPILE_COMMANDS" "$SOURCE_DIR/tests/test.c" >/dev/null; then
		fail "index --no-store-command failed"
	fi
	if [ -e "$commands_db" ]; then
		fail "index --no-store-command created a command database"
	fi
}

run_include_local_case()
{
	db=$tmpdir/local/.semindex/semindex.db

	if ! "$SEMINDEX" compiler --include-local --database "$db" -- \
	     cc -I"$SOURCE_DIR/tests/include" "$SOURCE_DIR/tests/test.c"; then
		fail "compiler --include-local failed"
	fi
	if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE local != 0")" -lt 1 ]; then
		fail "compiler --include-local did not store local records"
	fi
}

run_format_case()
{
	format=$1
	expect=$2
	out=$tmpdir/$format.out
	err=$tmpdir/$format.err
	db=$tmpdir/$format/.semindex/semindex.db

	if ! "$SEMINDEX" compiler --format "$format" --database "$db" -- \
	     cc -I"$SOURCE_DIR/tests/include" -c "$SOURCE_DIR/tests/test.c" >"$out" 2>"$err"; then
		cat "$err" >&2
		fail "compiler --format=$format failed"
	fi
	sed "s|$SOURCE_DIR/||g" "$out" >"$out.normalized"
	if ! diff -u "$SOURCE_DIR/$expect" "$out.normalized"; then
		fail "compiler output differs from $expect"
	fi
}

run_kernel_flags_case()
{
	db=$tmpdir/kernel/.semindex/semindex.db
	out=$tmpdir/kernel.out
	err=$tmpdir/kernel.err

	if ! "$SEMINDEX" compiler --database "$db" -- \
	     -D__STDC__ -Werror -Wbitwise -Wno-return-void \
	     -Wimplicit-fallthrough=5 -Werror=designated-init -Werror=date-time \
	     --arch=x86 --arch arm64 -mpreferred-stack-boundary=3 \
	     -mindirect-branch=thunk-extern -mindirect-branch-register \
	     -fno-allow-store-data-races -fzero-init-padding-bits=all \
	     -fdiagnostics-show-context=2 -fmin-function-alignment=16 \
	     -fconserve-stack -falign-jumps=1 "$SOURCE_DIR/tests/test.c" \
	     >"$out" 2>"$err"; then
		cat "$err" >&2
		fail "kernel compiler flags were not sanitized"
	fi
}

run_target_builtin_case()
{
	db=$tmpdir/target/.semindex/semindex.db
	err=$tmpdir/target.err

	if ! "$SEMINDEX" compiler --database "$db" -- cc \
	     -D__x86_64__ -m64 -m16 "$SOURCE_DIR/tests/compiler-target.c" \
	     >/dev/null 2>"$err"; then
		cat "$err" >&2
		fail "target builtin macro was not sanitized"
	fi
	if ! "$SEMINDEX" compiler --database "$db" -- cc \
	     -D __x86_64__ -m64 -m16 "$SOURCE_DIR/tests/compiler-target.c" \
	     >/dev/null 2>"$err"; then
		cat "$err" >&2
		fail "separate target builtin macro was not sanitized"
	fi
}

run_preprocessed_assembly_case()
{
	asm=$tmpdir/compiler-source.S
	db=$tmpdir/assembly/.semindex/semindex.db
	commands_db=$tmpdir/assembly/.semindex/commands.db
	compile_commands=$tmpdir/compile_commands.json
	index_db=$tmpdir/assembly-index/.semindex/semindex.db

	printf '%s\n' '#define ASM_VALUE 7' '#define ASM_USE ASM_VALUE' \
		'.long ASM_USE' >"$asm"
	if ! "$SEMINDEX" compiler --database "$db" -- "$asm"; then
		fail "preprocessed assembly compiler command failed"
	fi
	if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE symbol IN ('ASM_VALUE', 'ASM_USE') AND record = 0")" != 2 ]; then
		fail "preprocessed assembly macros were not indexed"
	fi
	if [ "$(sqlite3 "$db" "SELECT COUNT(*) FROM records WHERE symbol IN ('ASM_VALUE', 'ASM_USE') AND record = 1")" -lt 2 ]; then
		fail "preprocessed assembly macro uses were not indexed"
	fi
	if [ "$(sqlite3 "$commands_db" "SELECT COUNT(*) FROM commands WHERE file = '$asm'")" != 1 ]; then
		fail "preprocessed assembly compiler command was not stored"
	fi

	printf '[{"directory":"%s","file":"%s","arguments":["cc","-c","%s"]}]\n' \
		"$tmpdir" "$asm" "$asm" >"$compile_commands"
	if ! "$SEMINDEX" index --database "$index_db" \
	     --compile-commands "$compile_commands" "$asm" >/dev/null; then
		fail "preprocessed assembly index command failed"
	fi
	if [ "$(sqlite3 "$index_db" "SELECT COUNT(*) FROM records WHERE symbol IN ('ASM_VALUE', 'ASM_USE') AND record = 0")" != 2 ]; then
		fail "index command did not store preprocessed assembly macros"
	fi
}

if [ -z "${SEMINDEX:-}" ] || [ -z "${SOURCE_DIR:-}" ] || \
   [ -z "${COMPILE_COMMANDS:-}" ]; then
	fail "SEMINDEX, SOURCE_DIR, and COMPILE_COMMANDS must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

run_quiet_case
run_no_store_command_case
run_index_command_case
run_include_local_case
run_format_case default tests/test.c.expect
run_format_case dissect tests/test.c.dissect.expect
run_kernel_flags_case
run_target_builtin_case
run_preprocessed_assembly_case
