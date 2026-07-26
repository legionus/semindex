#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

if [ "$#" != 3 ]; then
	echo "usage: run-export-tests.sh NM LIBRARY EXPECTED" >&2
	exit 1
fi

nm=$1
library=$2
expected=$3

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

"$nm" -D --defined-only "$library" |
	awk 'NF >= 3 { print $3 }' |
	LC_ALL=C sort -u >"$tmpdir/actual"

LC_ALL=C sort -u "$expected" >"$tmpdir/expected"

if ! diff -u "$tmpdir/expected" "$tmpdir/actual"; then
	echo "FAIL: exported symbols differ for $library" >&2
	exit 1
fi
