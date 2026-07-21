#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

usage()
{
	cat <<EOF
Usage: scripts/analyze-trace.sh [OPTION]... TRACE.jsonl

Options:
  --limit=N             show the N slowest translation units (default: 20)
  -h, --help            display this help and exit
EOF
}

fail()
{
	echo "analyze-trace: $*" >&2
	exit 1
}

limit=20
trace=

while [ "$#" -gt 0 ]; do
	case $1 in
	--limit=*)
		limit=${1#*=}
		;;
	-h | --help)
		usage
		exit 0
		;;
	-*)
		usage >&2
		fail "unknown option: $1"
		;;
	*)
		[ -z "$trace" ] || fail "only one trace file may be specified"
		trace=$1
		;;
	esac
	shift
done

case $limit in
'' | *[!0-9]*)
	fail "--limit must be a positive integer"
	;;
esac
[ "$limit" -gt 0 ] || fail "--limit must be a positive integer"
[ -n "$trace" ] || {
	usage >&2
	fail "trace file is required"
}
[ -r "$trace" ] || fail "cannot read trace file: $trace"

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
summary=$tmpdir/summary
phases=$tmpdir/phases
slow=$tmpdir/slow

if ! awk -v summary="$summary" -v phases="$phases" -v slow="$slow" '
function malformed(reason)
{
	printf "analyze-trace: malformed line %d: %s\n", NR, reason > "/dev/stderr"
	invalid = 1
}

function extract(line, start_marker, end_marker, start, tail, end)
{
	start = index(line, start_marker)
	if (!start)
		return ""
	tail = substr(line, start + length(start_marker))
	end = index(tail, end_marker)
	if (!end)
		return ""
	return substr(tail, 1, end - 1)
}

{
	pid = extract($0, "{\"pid\":", ",\"command\":\"")
	command = extract($0, ",\"command\":\"", "\",\"source\":\"")
	source = extract($0, ",\"source\":\"", "\",\"phase\":\"")
	phase = extract($0, ",\"phase\":\"", "\",\"start_ns\":")
	start = extract($0, ",\"start_ns\":", ",\"duration_ns\":")
	duration = extract($0, ",\"duration_ns\":", "}")
	expected = "{\"pid\":" pid ",\"command\":\"" command "\",\"source\":\"" source \
		   "\",\"phase\":\"" phase "\",\"start_ns\":" start ",\"duration_ns\":" duration "}"

	if (pid !~ /^[0-9]+$/ || command == "" || source == "" || phase == "" ||
	    start !~ /^[0-9]+$/ || duration !~ /^[0-9]+$/ || $0 != expected) {
		malformed("unexpected JSONL record")
		next
	}
	start += 0
	duration += 0

	events++
	processes[pid] = 1
	sources[command SUBSEP source] = 1
	phase_count[phase]++
	phase_sum[phase] += duration
	if (duration > phase_max[phase])
		phase_max[phase] = duration
	if (!have_start || start < first_start) {
		first_start = start
		have_start = 1
	}
	end = start + duration
	if (end > last_end)
		last_end = end

	if (phase == "total") {
		total_sum += duration
		printf "%.0f\t%s\t%s\t%s\n", duration, pid, command, source > slow
	} else if (phase == "parse") {
		parse_sum += duration
	} else if (phase == "symbol_database") {
		symbol_sum += duration
	} else if (phase == "command_database") {
		command_sum += duration
	} else if (phase == "output") {
		output_sum += duration
	} else if (phase == "cleanup") {
		cleanup_sum += duration
	} else if (phase == "db.merge.begin") {
		lock_sum += duration
		lock_count++
		if (duration > lock_max)
			lock_max = duration
	}
}

END {
	if (invalid)
		exit 1
	if (!events) {
		print "analyze-trace: trace contains no events" > "/dev/stderr"
		exit 1
	}
	for (pid in processes)
		process_count++
	for (source in sources)
		source_count++
	printf "%d\t%d\t%d\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%d\t%.0f\n",
	       events, process_count, source_count, last_end - first_start, total_sum,
	       parse_sum, symbol_sum, command_sum, output_sum, cleanup_sum, lock_sum,
	       first_start, lock_count, lock_max > summary
	for (phase in phase_count)
		printf "%.0f\t%s\t%d\t%.0f\t%.0f\n", phase_sum[phase], phase,
		       phase_count[phase], phase_sum[phase] / phase_count[phase],
		       phase_max[phase] > phases
}
' "$trace"; then
	exit 1
fi

tab=$(printf '\t')
IFS="$tab" read -r events processes sources span total parse symbol command output cleanup \
	lock first_start lock_count lock_max <"$summary"

printf 'Trace: %s\n' "$trace"
printf 'Events: %s\n' "$events"
printf 'Processes: %s\n' "$processes"
printf 'Translation units: %s\n' "$sources"
awk -v span="$span" -v total="$total" 'BEGIN {
	printf "Trace span: %.3f s\n", span / 1000000000
	printf "Aggregate process time: %.3f s\n", total / 1000000000
}'

printf '\nTop-level process time (aggregated across processes):\n'
printf '%-20s %12s %10s\n' 'PHASE' 'TOTAL (s)' 'OF TOTAL'
awk -v total="$total" -v parse="$parse" -v symbol="$symbol" -v command="$command" \
	-v output="$output" -v cleanup="$cleanup" 'BEGIN {
	name[1] = "parse"; value[1] = parse
	name[2] = "symbol_database"; value[2] = symbol
	name[3] = "command_database"; value[3] = command
	name[4] = "output"; value[4] = output
	name[5] = "cleanup"; value[5] = cleanup
	for (i = 1; i <= 5; i++) {
		percent = total ? value[i] * 100 / total : 0
		printf "%-20s %12.3f %9.2f%%\n", name[i], value[i] / 1000000000, percent
	}
}'

printf '\nWriter lock acquisition:\n'
awk -v lock="$lock" -v count="$lock_count" -v max="$lock_max" -v symbol="$symbol" 'BEGIN {
	average = count ? lock / count : 0
	share = symbol ? lock * 100 / symbol : 0
	printf "  count: %d\n", count
	printf "  total: %.3f s\n", lock / 1000000000
	printf "  average: %.3f ms\n", average / 1000000
	printf "  maximum: %.3f ms\n", max / 1000000
	printf "  share of symbol_database: %.2f%%\n", share
}'

printf '\nPhase totals (nested phases overlap):\n'
printf '%-32s %8s %12s %12s %12s\n' 'PHASE' 'COUNT' 'TOTAL (s)' 'AVERAGE (ms)' 'MAXIMUM (ms)'
sort -t "$tab" -k1,1nr "$phases" | awk -F '\t' '{
	printf "%-32s %8d %12.3f %12.3f %12.3f\n", $2, $3, $1 / 1000000000,
	       $4 / 1000000, $5 / 1000000
}'

printf '\nSlowest translation units:\n'
printf '%12s %8s %-10s %s\n' 'TOTAL (s)' 'PID' 'COMMAND' 'SOURCE'
sort -t "$tab" -k1,1nr "$slow" | awk -F '\t' -v limit="$limit" 'NR <= limit {
	printf "%12.3f %8s %-10s %s\n", $1 / 1000000000, $2, $3, $4
}'
