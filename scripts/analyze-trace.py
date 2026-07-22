#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import heapq
import json
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


@dataclass
class PhaseStats:
    count: int = 0
    total: int = 0
    maximum: int = 0
    counter_count: int = 0
    items_in: int = 0
    items_out: int = 0


BASE_KEYS = {"pid", "command", "source", "phase", "start_ns", "duration_ns"}
COUNTED_KEYS = BASE_KEYS | {"items_in", "items_out"}
TOP_LEVEL_PHASES = (
    "parse",
    "fingerprint",
    "symbol_database",
    "command_database",
    "output",
    "cleanup",
)


def positive_integer(value):
    try:
        result = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a positive integer") from error
    if result <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return result


def parse_args():
    parser = argparse.ArgumentParser(
        prog="scripts/analyze-trace.py",
        usage="%(prog)s [OPTION]... TRACE.jsonl",
    )
    parser.add_argument("--limit", type=positive_integer, default=20,
        help="show the N slowest translation units (default: 20)")
    parser.add_argument("trace", type=Path, metavar="TRACE.jsonl")
    return parser.parse_args()


def is_integer(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def valid_event(event):
    if not isinstance(event, dict) or set(event) not in (BASE_KEYS, COUNTED_KEYS):
        return False
    if not is_integer(event["pid"]) or not is_integer(event["start_ns"]):
        return False
    if not is_integer(event["duration_ns"]):
        return False
    for name in ("command", "source", "phase"):
        if not isinstance(event[name], str) or not event[name]:
            return False
    if set(event) == COUNTED_KEYS:
        return is_integer(event["items_in"]) and is_integer(event["items_out"])
    return True


def unique_object(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            raise ValueError("duplicate object key")
        result[name] = value
    return result


def fail(message):
    print(f"analyze-trace: {message}", file=sys.stderr)
    raise SystemExit(1)


def percent(value, total):
    return value * 100 / total if total else 0


def main():
    args = parse_args()
    if not args.trace.is_file():
        fail(f"cannot read trace file: {args.trace}")

    phases = defaultdict(PhaseStats)
    processes = set()
    sources = set()
    slowest = []
    events = 0
    first_start = None
    last_end = 0

    try:
        trace_file = args.trace.open("rb")
    except OSError:
        fail(f"cannot read trace file: {args.trace}")

    with trace_file:
        for line_number, line in enumerate(trace_file, 1):
            try:
                event = json.loads(line.decode("utf-8"), object_pairs_hook=unique_object)
            except (json.JSONDecodeError, UnicodeDecodeError, ValueError):
                fail(f"malformed line {line_number}: unexpected JSONL record")
            if not valid_event(event):
                fail(f"malformed line {line_number}: unexpected JSONL record")

            events += 1
            processes.add(event["pid"])
            sources.add((event["command"], event["source"]))
            stats = phases[event["phase"]]
            stats.count += 1
            stats.total += event["duration_ns"]
            stats.maximum = max(stats.maximum, event["duration_ns"])
            if "items_in" in event:
                stats.counter_count += 1
                stats.items_in += event["items_in"]
                stats.items_out += event["items_out"]

            start = event["start_ns"]
            first_start = start if first_start is None else min(first_start, start)
            last_end = max(last_end, start + event["duration_ns"])
            if event["phase"] == "total":
                item = (event["duration_ns"], event["pid"], event["command"], event["source"])
                if len(slowest) < args.limit:
                    heapq.heappush(slowest, item)
                elif item > slowest[0]:
                    heapq.heapreplace(slowest, item)

    if not events:
        fail("trace contains no events")

    total = phases["total"].total
    span = last_end - first_start
    print(f"Trace: {args.trace}")
    print(f"Events: {events}")
    print(f"Processes: {len(processes)}")
    print(f"Translation units: {len(sources)}")
    print(f"Trace span: {span / 1_000_000_000:.3f} s")
    print(f"Aggregate process time: {total / 1_000_000_000:.3f} s")

    print("\nTop-level process time (aggregated across processes):")
    print(f"{'PHASE':<20} {'TOTAL (s)':>12} {'OF TOTAL':>10}")
    for name in TOP_LEVEL_PHASES:
        value = phases.get(name, PhaseStats()).total
        print(f"{name:<20} {value / 1_000_000_000:12.3f} {percent(value, total):9.2f}%")

    lock = phases.get("db.merge.begin", PhaseStats())
    symbol_total = phases.get("symbol_database", PhaseStats()).total
    print("\nWriter lock acquisition:")
    print(f"  count: {lock.count}")
    print(f"  total: {lock.total / 1_000_000_000:.3f} s")
    print(f"  average: {(lock.total / lock.count if lock.count else 0) / 1_000_000:.3f} ms")
    print(f"  maximum: {lock.maximum / 1_000_000:.3f} ms")
    print(f"  share of symbol_database: {percent(lock.total, symbol_total):.2f}%")

    stage = phases.get("db.stage_records", PhaseStats())
    merge = phases.get("db.merge.records_insert", PhaseStats())
    print("\nRecord flow:")
    if stage.counter_count and merge.counter_count:
        print(f"  in-memory records: {stage.items_in}")
        print(f"  private staging records: {stage.items_out} "
            f"({percent(stage.items_out, stage.items_in):.2f}% of input)")
        print(f"  main database attempts: {merge.items_in}")
        print(f"  inserted records: {merge.items_out} "
            f"({percent(merge.items_out, merge.items_in):.2f}% of attempts)")
        print(f"  ignored existing records: {merge.items_in - merge.items_out}")
    else:
        print("  unavailable (trace was recorded without counters)")

    files = phases.get("db.stage_files", PhaseStats())
    fingerprint_merge = phases.get("db.merge.fingerprints_insert", PhaseStats())
    print("\nFile fingerprint cache:")
    if files.counter_count and fingerprint_merge.counter_count:
        late = fingerprint_merge.items_in - files.items_out - fingerprint_merge.items_out
        print(f"  fingerprinted files: {files.items_in}")
        print(f"  reused files: {files.items_out} ({percent(files.items_out, files.items_in):.2f}%)")
        print(f"  main database attempts: {fingerprint_merge.items_in}")
        print(f"  inserted fingerprints: {fingerprint_merge.items_out}")
        print(f"  late concurrent matches: {max(0, late)}")
    else:
        print("  unavailable (trace was recorded without fingerprint counters)")

    print("\nPhase totals (nested phases overlap):")
    print(f"{'PHASE':<32} {'COUNT':>8} {'TOTAL (s)':>12} {'AVERAGE (ms)':>12} {'MAXIMUM (ms)':>12}")
    for name, stats in sorted(phases.items(), key=lambda item: item[1].total, reverse=True):
        average = stats.total / stats.count if stats.count else 0
        print(f"{name:<32} {stats.count:8d} {stats.total / 1_000_000_000:12.3f} "
            f"{average / 1_000_000:12.3f} {stats.maximum / 1_000_000:12.3f}")

    print("\nSlowest translation units:")
    print(f"{'TOTAL (s)':>12} {'PID':>8} {'COMMAND':<10} SOURCE")
    for duration, pid, command, source in sorted(slowest, reverse=True):
        print(f"{duration / 1_000_000_000:12.3f} {pid:8d} {command:<10} {source}")


if __name__ == "__main__":
    main()
