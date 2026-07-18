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

	set -- --compile-commands "$COMPILE_COMMANDS"
	if [ "$format" != "default" ]; then
		set -- "$@" --format "$format"
	fi

	if ! "$SEMINDEX" index "$@" "$SOURCE_DIR/$src" >"$out" 2>"$err"; then
		cat "$err" >&2
		cat "$out" >&2
		fail "$src did not index successfully"
	fi

	sed "s|$SOURCE_DIR/||g" "$out" >"$out.normalized"
	if ! diff -u "$SOURCE_DIR/$expect" "$out.normalized"; then
		fail "$src output differs from $expect"
	fi
}

if [ -z "${SEMINDEX:-}" ] || [ -z "${COMPILE_COMMANDS:-}" ] ||
   [ -z "${SOURCE_DIR:-}" ]; then
	fail "SEMINDEX, COMPILE_COMMANDS, and SOURCE_DIR must be set"
fi

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
	fail "usage: run-output-tests.sh <source> <expect> [format]"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

run_case "$1" "$2" "$tmpdir/test.out" "$tmpdir/test.err" "${3:-default}"
