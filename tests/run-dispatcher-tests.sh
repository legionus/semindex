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
