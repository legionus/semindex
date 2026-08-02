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

if [ "${SEMINDEX_TEST_GIT:-0}" != 1 ]; then
	exit 0
fi

repository=$tmpdir/repository
git_database=$tmpdir/git.db
git_source=$repository/source.c
mkdir "$repository"
cp "$SOURCE_DIR/tests/test.c" "$git_source"
git -C "$repository" init -q
git -C "$repository" -c user.name=Semindex -c user.email=semindex@example.com add source.c
git -C "$repository" -c user.name=Semindex -c user.email=semindex@example.com commit -qm initial
"$SEMINDEX" compiler --database="$git_database" --no-store-command --git-commit=auto -- \
	cc --no-default-config "$git_source"

printf '%s\n' '' 'int changed;' >>"$git_source"
"$SOURCE_RESOLVER_TEST" "$repository" "$git_source" "$git_database" source.c git-drifted
rm -f "$git_source"
"$SOURCE_RESOLVER_TEST" "$repository" "$repository/source.c" "$git_database" source.c git-missing
