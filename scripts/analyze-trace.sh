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
	counted = index($0, ",\"items_in\":") != 0
	items_in = 0
	items_out = 0
	if (counted) {
		duration = extract($0, ",\"duration_ns\":", ",\"items_in\":")
		items_in = extract($0, ",\"items_in\":", ",\"items_out\":")
		items_out = extract($0, ",\"items_out\":", "}")
		expected = "{\"pid\":" pid ",\"command\":\"" command "\",\"source\":\"" source \
			   "\",\"phase\":\"" phase "\",\"start_ns\":" start ",\"duration_ns\":" duration \
			   ",\"items_in\":" items_in ",\"items_out\":" items_out "}"
	} else {
		duration = extract($0, ",\"duration_ns\":", "}")
		expected = "{\"pid\":" pid ",\"command\":\"" command "\",\"source\":\"" source \
			   "\",\"phase\":\"" phase "\",\"start_ns\":" start ",\"duration_ns\":" duration "}"
	}

	if (pid !~ /^[0-9]+$/ || command == "" || source == "" || phase == "" ||
	    start !~ /^[0-9]+$/ || duration !~ /^[0-9]+$/ ||
	    (counted && (items_in !~ /^[0-9]+$/ || items_out !~ /^[0-9]+$/)) || $0 != expected) {
		malformed("unexpected JSONL record")
		next
	}
	start += 0
	duration += 0
	items_in += 0
	items_out += 0

	events++
	processes[pid] = 1
	sources[command SUBSEP source] = 1
	phase_count[phase]++
	phase_sum[phase] += duration
	if (duration > phase_max[phase])
		phase_max[phase] = duration
	if (counted) {
		phase_counter_count[phase]++
		phase_items_in[phase] += items_in
		phase_items_out[phase] += items_out
	}
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
	printf "%d\t%d\t%d\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%d\t%.0f" \
	       "\t%d\t%.0f\t%.0f\t%d\t%.0f\t%.0f\t%d\t%.0f\t%.0f\n",
	       events, process_count, source_count, last_end - first_start, total_sum,
	       parse_sum, symbol_sum, command_sum, output_sum, cleanup_sum, lock_sum,
	       first_start, lock_count, lock_max, phase_counter_count["db.stage_files"],
	       phase_items_in["db.stage_files"], phase_items_out["db.stage_files"],
	       phase_counter_count["db.stage_records"],
	       phase_items_in["db.stage_records"], phase_items_out["db.stage_records"],
	       phase_counter_count["db.merge.records_insert"], phase_items_in["db.merge.records_insert"],
	       phase_items_out["db.merge.records_insert"] > summary
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
	lock first_start lock_count lock_max file_count file_in file_cached stage_count stage_in stage_out \
	merge_count merge_in merge_out <"$summary"

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

printf '\nRecord flow:\n'
if [ "$stage_count" -gt 0 ] && [ "$merge_count" -gt 0 ]; then
	awk -v stage_in="$stage_in" -v stage_out="$stage_out" -v merge_in="$merge_in" \
		-v merge_out="$merge_out" 'BEGIN {
		staged_percent = stage_in ? stage_out * 100 / stage_in : 0
		inserted_percent = merge_in ? merge_out * 100 / merge_in : 0
		printf "  in-memory records: %.0f\n", stage_in
		printf "  private staging records: %.0f (%.2f%% of input)\n", stage_out, staged_percent
		printf "  main database attempts: %.0f\n", merge_in
		printf "  inserted records: %.0f (%.2f%% of attempts)\n", merge_out, inserted_percent
		printf "  ignored existing records: %.0f\n", merge_in - merge_out
	}'
else
	printf '  unavailable (trace was recorded without counters)\n'
fi

printf '\nFile fingerprint cache:\n'
if [ "$file_count" -gt 0 ]; then
	awk -v files="$file_in" -v cached="$file_cached" 'BEGIN {
		percent = files ? cached * 100 / files : 0
		printf "  fingerprinted files: %.0f\n", files
		printf "  reused files: %.0f (%.2f%%)\n", cached, percent
	}'
else
	printf '  unavailable (trace was recorded without file counters)\n'
fi

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
