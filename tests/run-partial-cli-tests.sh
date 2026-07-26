#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

run_case()
{
	command=$1
	database=$2
	out=$3
	err=$4
	shift 4

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
	if [ "$(sqlite3 "$database" "SELECT COUNT(*) FROM records WHERE symbol = 'test_pp_pos'")" != 1 ]; then
		fail "$command omitted a recovered symbol from the database"
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

printf '[{"directory":"%s","file":"%s","arguments":["cc","--no-default-config","%s"]}]\n' \
	"$SOURCE_DIR" "$source" "$source" >"$compile_commands"

run_case compiler "$compiler_db" "$tmpdir/compiler.out" "$tmpdir/compiler.err" \
	--format=dissect --no-store-command --database "$compiler_db" -- \
	cc --no-default-config "$source"
run_case index "$index_db" "$tmpdir/index.out" "$tmpdir/index.err" \
	--format=dissect --no-store-command --database "$index_db" \
	--compile-commands "$compile_commands" "$source"
