#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

run_scope()
{
	scope=$1
	expect=$2
	out=$3
	err=$4

	if ! "$SEMINDEX" index --compile-commands "$COMPILE_COMMANDS" \
	     --scope "$scope" "$SOURCE_DIR/tests/scope/use.c" >"$out" 2>"$err"; then
		cat "$err" >&2
		cat "$out" >&2
		fail "scope=$scope did not index successfully"
	fi

	sed "s|$SOURCE_DIR/||g" "$out" >"$out.normalized"
	if ! diff -u "$SOURCE_DIR/$expect" "$out.normalized"; then
		fail "scope=$scope output differs from $expect"
	fi
}

if [ -z "${SEMINDEX:-}" ] || [ -z "${COMPILE_COMMANDS:-}" ] ||
   [ -z "${SOURCE_DIR:-}" ]; then
	fail "SEMINDEX, COMPILE_COMMANDS, and SOURCE_DIR must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

run_scope file tests/scope/use.c.file.expect "$tmpdir/file.out" "$tmpdir/file.err"
run_scope project tests/scope/use.c.project.expect "$tmpdir/project.out" "$tmpdir/project.err"
