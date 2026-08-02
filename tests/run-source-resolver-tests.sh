#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

if [ -z "${SEMINDEX:-}" ] || [ -z "${SOURCE_RESOLVER_TEST:-}" ] || [ -z "${SOURCE_DIR:-}" ]; then
	echo "FAIL: SEMINDEX, SOURCE_RESOLVER_TEST, and SOURCE_DIR must be set" >&2
	exit 1
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
database=$tmpdir/semindex.db
source=$tmpdir/source.c

cp "$SOURCE_DIR/tests/test.c" "$source"
"$SEMINDEX" compiler --database="$database" --no-store-command -- cc --no-default-config "$source"

"$SOURCE_RESOLVER_TEST" "$SOURCE_DIR" "$SOURCE_DIR/tests/test.c" "$database" "$source" current
printf '%s\n' '' 'int changed;' >>"$source"
"$SOURCE_RESOLVER_TEST" "$SOURCE_DIR" "$SOURCE_DIR/tests/test.c" "$database" "$source" drifted
rm -f "$source"
"$SOURCE_RESOLVER_TEST" "$SOURCE_DIR" "$SOURCE_DIR/tests/test.c" "$database" "$source" missing
