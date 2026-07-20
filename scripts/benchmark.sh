#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

usage()
{
	cat <<EOF
Usage: scripts/benchmark.sh --baseline=PATH --candidate=PATH [OPTION]...

Options:
  --baseline=PATH       baseline semindex executable
  --candidate=PATH      candidate semindex executable
  --iterations=N        measured iterations per executable (default: 5)
  --passes=N             source-set passes per measurement (default: 5)
  -h, --help            display this help and exit
EOF
}

fail()
{
	echo "benchmark: $*" >&2
	exit 1
}

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
baseline=
candidate=
iterations=5
passes=5

while [ "$#" -gt 0 ]; do
	case $1 in
	--baseline=*)
		baseline=${1#*=}
		;;
	--candidate=*)
		candidate=${1#*=}
		;;
	--iterations=*)
		iterations=${1#*=}
		;;
	--passes=*)
		passes=${1#*=}
		;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		usage >&2
		fail "unknown option: $1"
		;;
	esac
	shift
done

[ -n "$baseline" ] || fail "--baseline is required"
[ -n "$candidate" ] || fail "--candidate is required"
[ -x "$baseline" ] || fail "baseline is not executable: $baseline"
[ -x "$candidate" ] || fail "candidate is not executable: $candidate"
case $iterations in
'' | *[!0-9]*)
	fail "iterations must be a positive integer"
	;;
esac
[ "$iterations" -gt 0 ] || fail "iterations must be a positive integer"
case $passes in
'' | *[!0-9]*)
	fail "passes must be a positive integer"
	;;
esac
[ "$passes" -gt 0 ] || fail "passes must be a positive integer"
[ -x /usr/bin/time ] || fail "/usr/bin/time is required"
command -v sqlite3 >/dev/null 2>&1 || fail "sqlite3 is required"

baseline=$(CDPATH= cd -- "$(dirname -- "$baseline")" && pwd)/$(basename -- "$baseline")
candidate=$(CDPATH= cd -- "$(dirname -- "$candidate")" && pwd)/$(basename -- "$candidate")
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
results=$tmpdir/results.tsv
callgraph_source=$tmpdir/callgraph-benchmark.c

sources='tests/test.c tests/test6.c tests/test7.c tests/test11.c tests/test14.c tests/test15.c'

: >"$callgraph_source"
function=0
while [ "$function" -lt 256 ]; do
	printf 'static void callee_%d(void) {}\n' "$function" >>"$callgraph_source"
	function=$((function + 1))
done
printf '%s\n' 'void call_all(void)' '{' >>"$callgraph_source"
function=0
while [ "$function" -lt 256 ]; do
	printf '\tcallee_%d();\n' "$function" >>"$callgraph_source"
	function=$((function + 1))
done
printf '%s\n' '}' >>"$callgraph_source"

run_one()
{
	label=$1
	binary=$2
	iteration=$3
	run_dir=$tmpdir/$iteration-$label
	state=$run_dir/state
	times=$run_dir/times.tsv

	mkdir -p "$state"
	: >"$times"
	pass=1
	while [ "$pass" -le "$passes" ]; do
		for source in $sources "$callgraph_source"; do
			case $source in
			/*)
				source_path=$source
				;;
			*)
				source_path=$root/$source
				;;
			esac
			/usr/bin/time -a -o "$times" -f '%e\t%U\t%S\t%M' \
				"$binary" compiler \
				--database="$state/semindex.db" \
				--commands-database="$state/commands.db" -- \
				cc -I"$root/tests/include" "$source_path" \
				>/dev/null
		done
		pass=$((pass + 1))
	done

	set -- $(awk -F '\t' '
		{ wall += $1; user += $2; sys += $3; if ($4 > rss) rss = $4 }
		END { printf "%.6f %.6f %.6f %d", wall, user, sys, rss }
	' "$times")
	db_bytes=$(find "$state" -maxdepth 1 -type f -printf '%s\n' |
		awk '{ total += $1 } END { print total + 0 }')
	records=$(sqlite3 "$state/semindex.db" 'SELECT count(*) FROM records')
	printf '%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$label" "$iteration" "$1" "$2" "$3" "$4" "$db_bytes" "$records" >>"$results"
}

median()
{
	label=$1
	column=$2
	awk -F '\t' -v label="$label" -v column="$column" '$1 == label { print $column }' "$results" |
		sort -n | awk '
			{ value[NR] = $1 }
			END {
				if (NR % 2)
					print value[(NR + 1) / 2]
				else
					printf "%.6f\n", (value[NR / 2] + value[NR / 2 + 1]) / 2
			}'
}

# Warm up loader, Clang, and filesystem metadata caches without retaining data.
run_one warmup-baseline "$baseline" 0
run_one warmup-candidate "$candidate" 0
: >"$results"

iteration=1
while [ "$iteration" -le "$iterations" ]; do
	if [ $((iteration % 2)) -eq 1 ]; then
		run_one baseline "$baseline" "$iteration"
		run_one candidate "$candidate" "$iteration"
	else
		run_one candidate "$candidate" "$iteration"
		run_one baseline "$baseline" "$iteration"
	fi
	iteration=$((iteration + 1))
done

printf 'label\titeration\twall_s\tuser_s\tsys_s\tmaxrss_kb\tdb_bytes\trecords\n'
cat "$results"
printf '\nmedian\twall_s\tuser_s\tsys_s\tmaxrss_kb\tdb_bytes\trecords\n'
for label in baseline candidate; do
	printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$label" "$(median "$label" 3)" "$(median "$label" 4)" \
		"$(median "$label" 5)" "$(median "$label" 6)" \
		"$(median "$label" 7)" "$(median "$label" 8)"
done

baseline_wall=$(median baseline 3)
candidate_wall=$(median candidate 3)
baseline_rss=$(median baseline 6)
candidate_rss=$(median candidate 6)
baseline_db=$(median baseline 7)
candidate_db=$(median candidate 7)
awk -v baseline_wall="$baseline_wall" -v candidate_wall="$candidate_wall" \
	-v baseline_rss="$baseline_rss" -v candidate_rss="$candidate_rss" \
	-v baseline_db="$baseline_db" -v candidate_db="$candidate_db" 'BEGIN {
	print ""
	if (baseline_wall != 0)
		printf "wall time change: %+.2f%%\n", (candidate_wall - baseline_wall) * 100 / baseline_wall
	if (baseline_rss != 0)
		printf "peak RSS change: %+.2f%%\n", (candidate_rss - baseline_rss) * 100 / baseline_rss
	if (baseline_db != 0)
		printf "database size change: %+.2f%%\n", (candidate_db - baseline_db) * 100 / baseline_db
}'
