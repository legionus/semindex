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
	src=$1
	expect=$2
	out=$3
	err=$4
	format=$5

	set -- compiler
	if [ "$format" != "default" ]; then
		set -- "$@" --format "$format"
	fi
	set -- "$@" -- cc -I"$SOURCE_DIR/tests/include" -c "$SOURCE_DIR/$src" -o "$out.o"

	if ! "$SEMINDEX" "$@" >"$out" 2>"$err"; then
		cat "$err" >&2
		cat "$out" >&2
		fail "$src did not index successfully"
	fi

	sed "s|$SOURCE_DIR/||g" "$out" >"$out.normalized"
	if ! diff -u "$SOURCE_DIR/$expect" "$out.normalized"; then
		fail "$src output differs from $expect"
	fi
}

if [ -z "${SEMINDEX:-}" ] || [ -z "${SOURCE_DIR:-}" ]; then
	fail "SEMINDEX and SOURCE_DIR must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

run_case tests/test.c tests/test.c.expect "$tmpdir/default.out" "$tmpdir/default.err" default
run_case tests/test.c tests/test.c.dissect.expect "$tmpdir/dissect.out" "$tmpdir/dissect.err" dissect
