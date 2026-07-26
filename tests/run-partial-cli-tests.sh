#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

check_output()
{
	command=$1
	out=$2
	err=$3
	shift 3

	if ! "$SEMINDEX" "$command" "$@" >"$out" 2>"$err"; then
		cat "$err" >&2
		fail "$command rejected a source file with recoverable errors"
	fi

	sed "s|$SOURCE_DIR/||g" "$out" >"$out.normalized"

	if ! diff -u "$SOURCE_DIR/tests/x_fixes_00.c.dissect.expect" "$out.normalized"; then
		fail "$command partial index output differs"
	fi
	if ! grep -q 'error:' "$err"; then
		fail "$command omitted diagnostics for a partial index"
	fi
}

check_database()
{
	command=$1
	database=$2
	shift 2

	if ! "$SEMINDEX" "$command" "$@" >/dev/null 2>"$tmpdir/$command-store.err"; then
		cat "$tmpdir/$command-store.err" >&2
		fail "$command rejected a source file with recoverable errors"
	fi

	if [ "$(sqlite3 "$database" "SELECT COUNT(*) FROM records WHERE symbol = 'test_pp_pos'")" != 1 ]; then
		fail "$command omitted a recovered symbol from the database"
	fi
	if [ "$(sqlite3 "$database" "SELECT COUNT(*) FROM records WHERE symbol = 'i_func3' AND record = 0 AND action = 1")" != 1 ]; then
		fail "$command retained a declaration instead of the recovered definition"
	fi
	if [ "$(sqlite3 "$database" "SELECT COUNT(*) FROM records WHERE symbol = 'FMS_O.m1'")" != 2 ]; then
		fail "$command omitted a promoted field or its initializer use"
	fi
	if [ "$(sqlite3 "$database" "SELECT COUNT(*) FROM records WHERE symbol = 'NO_FMS_S.m'")" != 2 ]; then
		fail "$command omitted an anonymous-union field"
	fi
}

if [ -z "${SEMINDEX:-}" ] || [ -z "${SOURCE_DIR:-}" ]; then
	fail "SEMINDEX and SOURCE_DIR must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

source=$SOURCE_DIR/tests/x_fixes_00.c
compiler_db=$tmpdir/compiler.db
index_db=$tmpdir/index.db
compile_commands=$tmpdir/compile_commands.json

printf '[{"directory":"%s","file":"%s","arguments":["cc","--no-default-config","-fms-extensions","%s"]}]\n' \
	"$SOURCE_DIR" "$source" "$source" >"$compile_commands"

check_output compiler "$tmpdir/compiler.out" "$tmpdir/compiler.err" \
	--format=dissect --no-store-command --database "$compiler_db" -- \
	cc --no-default-config -fms-extensions "$source"
check_database compiler "$compiler_db" --no-store-command --database "$compiler_db" -- \
	cc --no-default-config -fms-extensions "$source"
check_output index "$tmpdir/index.out" "$tmpdir/index.err" \
	--format=dissect --no-store-command --database "$index_db" \
	--compile-commands "$compile_commands" "$source"
check_database index "$index_db" --no-store-command --database "$index_db" \
	--compile-commands "$compile_commands" "$source"
