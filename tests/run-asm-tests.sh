#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

if [ "$#" -ne 2 ]; then
	fail "usage: run-asm-tests.sh SEMINDEX SOURCE_DIR"
fi

semindex=$1
source_dir=$2
source=$source_dir/tests/test21.S
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT
database=$tmpdir/semindex.db

"$semindex" compiler --database="$database" --no-store-command --root="$source_dir" \
	-- cc --no-default-config "$source" >/dev/null

calls=$(sqlite3 "$database" \
	"SELECT context || ':' || symbol FROM records
	 WHERE record = 1 AND action = 3 AND kind = 7 ORDER BY line")
expected='x86_caller:x86_target
arm64_caller:arm64_target
riscv_caller:riscv_target
riscv_caller:riscv_jal_target
s390_caller:s390_target
m68k_caller:m68k_target
ent_caller:ent_target'

if [ "$calls" != "$expected" ]; then
	fail "stored assembly calls differ"
fi

if sqlite3 "$database" \
	"SELECT symbol FROM records WHERE symbol IN ('rax', 'x0', 't0', 'a0')" | grep -q .; then
	fail "register-indirect assembly call was stored"
fi

"$semindex" search --database="$database" x86_target >"$tmpdir/search.out"

if ! grep -q '^(--r).*x86_caller.*call x86_target$' "$tmpdir/search.out"; then
	fail "assembly call search failed"
fi

plan=$(sqlite3 "$database" \
	"EXPLAIN QUERY PLAN SELECT files.path FROM records JOIN files
	 ON files.id = records.file_id WHERE records.symbol = 'x86_target'")

if ! printf '%s\n' "$plan" | grep -q 'SEARCH records USING PRIMARY KEY (symbol=?)'; then
	fail "exact assembly call search does not use the records primary key"
fi
