#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

if [ "$#" != 9 ]; then
	fail "usage: run-dispatcher-tests.sh DISPATCHER CC COMPILER INDEX SEARCH CALLGRAPH COMPILE_COMMANDS LSP MCP"
fi

dispatcher=$1
shift
cc=$1
compiler=$2
index=$3
search=$4
callgraph=$5
compile_commands=$6
lsp=$7
mcp=$8

for helper in "$@"; do
	if ! "$helper" --help >/dev/null; then
		fail "direct helper failed: $helper"
	fi
done

for command in cc compiler index search callgraph compile-commands lsp mcp; do
	if ! "$dispatcher" "$command" --help >/dev/null; then
		fail "dispatched helper failed: $command"
	fi
done

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

cat >"$tmpdir/semindex-example" <<'EOF'
#!/bin/sh
printf '%s\n' "$@"
EOF
chmod +x "$tmpdir/semindex-example"

if ! PATH="$tmpdir:$PATH" "$dispatcher" example first second >"$tmpdir/plugin.out"; then
	fail "external command failed"
fi
if [ "$(cat "$tmpdir/plugin.out")" != "first
second" ]; then
	fail "dispatcher rewrote external command arguments"
fi

if "$dispatcher" '../search' >"$tmpdir/invalid.out" 2>"$tmpdir/invalid.err"; then
	fail "invalid command name succeeded"
fi
if ! grep -q 'invalid command name' "$tmpdir/invalid.err"; then
	fail "invalid command diagnostic differs"
fi

set +e
PATH="$tmpdir" "$dispatcher" missing >"$tmpdir/missing.out" 2>"$tmpdir/missing.err"
status=$?
set -e

if [ "$status" != 127 ]; then
	fail "missing command returned status $status instead of 127"
fi
if ! grep -q 'failed to execute semindex-missing' "$tmpdir/missing.err"; then
	fail "missing command diagnostic differs"
fi

for binary in "$dispatcher" "$@"; do
	if ldd "$binary" | grep -q 'liblibsemindex_'; then
		fail "executable links against an internal shared library: $binary"
	fi
done

repository=$tmpdir/repository
nested=$repository/src/nested
mkdir -p "$repository/.git" "$nested"
cat >"$repository/example.c" <<'EOF'
static void callee(void)
{
}

void caller(void)
{
	callee();
}
EOF

(
	cd "$nested"
	"$compiler" -- cc --no-default-config ../../example.c >/dev/null
)

if [ ! -f "$repository/.semindex/semindex.db" ] || [ ! -f "$repository/.semindex/commands.db" ]; then
	fail "compiler did not create default databases at the repository root"
fi

if ! (cd "$nested" && "$search" caller) >"$tmpdir/search.out"; then
	fail "search did not use the repository database"
fi

if ! grep -q 'caller' "$tmpdir/search.out"; then
	fail "search did not find the repository symbol"
fi

if ! (cd "$nested" && "$callgraph" --callees=caller) >"$tmpdir/callgraph.out"; then
	fail "callgraph did not use the repository database"
fi

if ! grep -q 'callee' "$tmpdir/callgraph.out"; then
	fail "callgraph did not find the repository call"
fi

if ! (cd "$nested" && "$compile_commands") >"$tmpdir/compile-commands.json"; then
	fail "compile-commands did not use the repository database"
fi

if ! grep -q 'example.c' "$tmpdir/compile-commands.json"; then
	fail "compile-commands did not export the repository command"
fi
