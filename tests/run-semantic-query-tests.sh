#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

if [ -z "${SEMINDEX:-}" ] || [ -z "${SEMINDEX_COMPILER:-}" ] || \
	[ -z "${SEMANTIC_QUERY_TEST:-}" ] || [ -z "${SOURCE_DIR:-}" ]; then
	echo "FAIL: semantic query test environment is incomplete" >&2
	exit 1
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
database=$tmpdir/semindex.db

cd "$SOURCE_DIR"

for variant in general debug; do
	"$SEMINDEX_COMPILER" --root="$SOURCE_DIR" --database="$database" \
		--variant="$variant" --no-store-command -- \
		cc --no-default-config tests/test11.c
done

"$SEMANTIC_QUERY_TEST" "$SOURCE_DIR" "$SOURCE_DIR/tests/test11.c" "$database"
