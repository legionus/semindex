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
	if sqlite3 "$db" "SELECT 1 FROM compile_commands" >/dev/null 2>&1; then
		fail "compiler command storage is still enabled"
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

	if ! "$SEMINDEX" compiler --database "$db" -- cc \
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

if [ -z "${SEMINDEX:-}" ] || [ -z "${SOURCE_DIR:-}" ]; then
	fail "SEMINDEX and SOURCE_DIR must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

run_quiet_case
run_format_case default tests/test.c.expect
run_format_case dissect tests/test.c.dissect.expect
run_kernel_flags_case
