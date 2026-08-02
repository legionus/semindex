#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

make_source()
{
	path=$1
	name=$2

	printf 'int %s(void) { return 0; }\n' "$name" >"$path"
}

check_index()
{
	database=$1
	symbol=$2

	if [ "$(sqlite3 "$database" "SELECT COUNT(*) FROM records WHERE symbol = '$symbol'")" -lt 1 ]; then
		fail "wrapper did not index $symbol"
	fi
}

run_explicit_case()
{
	database=$tmpdir/explicit/.semindex/semindex.db
	commands=$tmpdir/explicit/.semindex/commands.db
	source=$tmpdir/explicit.c
	object=$tmpdir/explicit.o

	make_source "$source" explicit_symbol
	"$SEMINDEX" cc --database "$database" -- "$CC" -c "$source" -o "$object"

	[ -f "$object" ] || fail "explicit wrapper did not run the compiler"
	check_index "$database" explicit_symbol

	if [ "$(sqlite3 "$commands" "SELECT COUNT(*) FROM commands")" != 1 ]; then
		fail "explicit wrapper did not store the compiler command"
	fi
	if [ "$(sqlite3 "$commands" "SELECT substr(arguments, 1, instr(arguments, char(0)) - 1) FROM commands")" != "$CC" ]; then
		fail "stored command does not start with the real compiler"
	fi
}

run_real_cc_case()
{
	database=$tmpdir/real-cc/.semindex/semindex.db
	source=$tmpdir/real-cc.c
	object=$tmpdir/real-cc.o

	make_source "$source" real_cc_symbol
	REAL_CC="$CC" "$SEMINDEX_CC" --database "$database" -- -c "$source" -o "$object"

	[ -f "$object" ] || fail "REAL_CC wrapper did not run the compiler"
	check_index "$database" real_cc_symbol
}

run_launcher_case()
{
	database=$tmpdir/launcher/.semindex/semindex.db
	source=$tmpdir/launcher.c
	object=$tmpdir/launcher.o

	make_source "$source" launcher_symbol
	SEMINDEX_DATABASE="$database" "$SEMINDEX_CC" "$CC" -c "$source" -o "$object"

	[ -f "$object" ] || fail "launcher wrapper did not run the compiler"
	check_index "$database" launcher_symbol
}

run_passthrough_case()
{
	database=$tmpdir/passthrough/.semindex/semindex.db
	log=$tmpdir/passthrough.log
	fake=$tmpdir/fake-cc

	cat >"$fake" <<'EOF'
#!/bin/sh
printf '%s\n' "$@" >"$SEMINDEX_TEST_LOG"
exit "${SEMINDEX_TEST_STATUS:-0}"
EOF
	chmod +x "$fake"

	SEMINDEX_DATABASE="$database" SEMINDEX_TEST_LOG="$log" \
		"$SEMINDEX_CC" "$fake" @arguments.rsp

	if [ "$(cat "$log")" != @arguments.rsp ]; then
		fail "response-file invocation was not passed through unchanged"
	fi
	[ ! -e "$database" ] || fail "pass-through invocation created a database"

	set +e
	SEMINDEX_TEST_LOG="$log" SEMINDEX_TEST_STATUS=37 \
		"$SEMINDEX_CC" "$fake" --version
	status=$?
	set -e

	if [ "$status" != 37 ]; then
		fail "wrapper changed compiler exit status $status"
	fi
}

run_recursion_case()
{
	set +e
	SEMINDEX_CC_ACTIVE=1 "$SEMINDEX_CC" "$CC" --version \
		>"$tmpdir/recursion.out" 2>"$tmpdir/recursion.err"
	status=$?
	set -e

	if [ "$status" != 127 ]; then
		fail "recursive invocation returned status $status instead of 127"
	fi
	grep -q "recursive compiler invocation" "$tmpdir/recursion.err" || \
		fail "recursive invocation omitted its diagnostic"
}

run_error_policy_case()
{
	database=$tmpdir/unusable-database
	source=$tmpdir/error-policy.c
	warn_object=$tmpdir/warn.o
	fail_object=$tmpdir/fail.o
	ignore_object=$tmpdir/ignore.o

	mkdir "$database"
	make_source "$source" error_policy_symbol

	if ! "$SEMINDEX" cc --database "$database" --index-errors=warn -- \
	     "$CC" -c "$source" -o "$warn_object" 2>"$tmpdir/warn.err"; then
		fail "warn policy blocked the compiler"
	fi
	[ -f "$warn_object" ] || fail "warn policy did not run the compiler"
	grep -q "continuing" "$tmpdir/warn.err" || fail "warn policy omitted its diagnostic"

	if "$SEMINDEX" cc --database "$database" --index-errors=fail -- \
	   "$CC" -c "$source" -o "$fail_object" 2>"$tmpdir/fail.err"; then
		fail "fail policy accepted an indexing failure"
	fi
	[ ! -e "$fail_object" ] || fail "fail policy ran the compiler"

	if ! "$SEMINDEX" cc --database "$database" --index-errors=ignore -- \
	     "$CC" -c "$source" -o "$ignore_object" 2>"$tmpdir/ignore.err"; then
		fail "ignore policy blocked the compiler"
	fi
	[ -f "$ignore_object" ] || fail "ignore policy did not run the compiler"
	[ ! -s "$tmpdir/ignore.err" ] || fail "ignore policy emitted indexing diagnostics"
}

run_parallel_case()
{
	database=$tmpdir/parallel/.semindex/semindex.db
	pids=
	i=1

	while [ "$i" -le 8 ]; do
		source=$tmpdir/parallel-$i.c
		object=$tmpdir/parallel-$i.o

		make_source "$source" parallel_symbol_$i
		"$SEMINDEX" cc --no-store-command --database "$database" -- \
			"$CC" -c "$source" -o "$object" &
		pids="$pids $!"
		i=$((i + 1))
	done

	for pid in $pids; do
		wait "$pid" || fail "parallel wrapper invocation failed"
	done

	if [ "$(sqlite3 "$database" "SELECT COUNT(DISTINCT symbol) FROM records WHERE symbol GLOB 'parallel_symbol_*'")" != 8 ]; then
		fail "parallel wrapper invocations lost records"
	fi
}

if [ -z "${SEMINDEX:-}" ] || [ -z "${SEMINDEX_CC:-}" ] || \
   [ -z "${CC:-}" ]; then
	fail "SEMINDEX, SEMINDEX_CC, and CC must be set"
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

run_explicit_case
run_real_cc_case
run_launcher_case
run_passthrough_case
run_recursion_case
run_error_policy_case
run_parallel_case
