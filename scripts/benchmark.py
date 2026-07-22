#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import os
import sqlite3
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from statistics import median


SOURCES = (
    "tests/test.c",
    "tests/test6.c",
    "tests/test7.c",
    "tests/test11.c",
    "tests/test14.c",
    "tests/test15.c",
)


@dataclass
class Result:
    label: str
    iteration: int
    wall: float
    user: float
    system: float
    maxrss: int
    db_bytes: int
    records: int


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
        prog="scripts/benchmark.py",
        usage="%(prog)s --baseline=PATH --candidate=PATH [OPTION]...",
    )
    parser.add_argument("--baseline", required=True, type=Path,
        help="baseline semindex executable")
    parser.add_argument("--candidate", required=True, type=Path,
        help="candidate semindex executable")
    parser.add_argument("--iterations", type=positive_integer, default=5,
        help="measured iterations per executable (default: 5)")
    parser.add_argument("--passes", type=positive_integer, default=5,
        help="source-set passes per measurement (default: 5)")
    return parser.parse_args()


def fail(message):
    print(f"benchmark: {message}", file=sys.stderr)
    raise SystemExit(1)


def executable(path, label):
    path = path.resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        fail(f"{label} is not executable: {path}")
    return path


def create_fixtures(directory):
    callgraph = directory / "callgraph-benchmark.c"
    with callgraph.open("w", encoding="utf-8") as output:
        for function in range(256):
            output.write(f"static void callee_{function}(void) {{}}\n")
        output.write("void call_all(void)\n{\n")
        for function in range(256):
            output.write(f"\tcallee_{function}();\n")
        output.write("}\n")

    shared_header = directory / "shared-benchmark.h"
    with shared_header.open("w", encoding="utf-8") as output:
        output.write("struct benchmark_shared {\n")
        for field in range(256):
            output.write(f"\tint field_{field};\n")
        output.write("};\n")

    shared_sources = []
    for worker in range(8):
        source = directory / f"shared-benchmark-{worker}.c"
        source.write_text(
            '#include "shared-benchmark.h"\n'
            f"int read_shared_{worker}(struct benchmark_shared *p) "
            f"{{ return p->field_{worker}; }}\n",
            encoding="utf-8",
        )
        shared_sources.append(source)
    return callgraph, shared_sources


def read_timings(path):
    wall = 0.0
    user = 0.0
    system = 0.0
    maxrss = 0
    with path.open(encoding="ascii") as timings:
        for line_number, line in enumerate(timings, 1):
            fields = line.rstrip("\n").split("\t")
            if len(fields) != 4:
                fail(f"malformed timing record {line_number}: {path}")
            try:
                wall += float(fields[0])
                user += float(fields[1])
                system += float(fields[2])
                maxrss = max(maxrss, int(fields[3]))
            except ValueError:
                fail(f"malformed timing record {line_number}: {path}")
    return wall, user, system, maxrss


def count_records(database):
    try:
        with sqlite3.connect(database) as connection:
            row = connection.execute("SELECT count(*) FROM records").fetchone()
    except sqlite3.Error as error:
        fail(f"sqlite: {error}")
    return row[0]


def run_one(root, temporary, label, binary, iteration, passes, sources):
    run_directory = temporary / f"{iteration}-{label}"
    state = run_directory / "state"
    state.mkdir(parents=True)
    timings = run_directory / "times.tsv"
    timings.touch()

    for _ in range(passes):
        for source in sources:
            command = (
                "/usr/bin/time", "-a", "-o", str(timings), "-f", "%e\t%U\t%S\t%M",
                str(binary), "compiler", f"--database={state / 'semindex.db'}",
                f"--commands-database={state / 'commands.db'}", "--", "cc",
                f"-I{root / 'tests/include'}", str(source),
            )
            try:
                subprocess.run(command, stdout=subprocess.DEVNULL, check=True)
            except subprocess.CalledProcessError as error:
                fail(f"semindex exited with status {error.returncode}: {source}")

    wall, user, system, maxrss = read_timings(timings)
    db_bytes = sum(path.stat().st_size for path in state.iterdir() if path.is_file())
    records = count_records(state / "semindex.db")
    return Result(label, iteration, wall, user, system, maxrss, db_bytes, records)


def result_median(results, label, attribute):
    return median(getattr(result, attribute) for result in results if result.label == label)


def format_integer_median(value):
    return str(int(value)) if float(value).is_integer() else f"{value:.6f}"


def print_results(results):
    print("label\titeration\twall_s\tuser_s\tsys_s\tmaxrss_kb\tdb_bytes\trecords")
    for result in results:
        print(f"{result.label}\t{result.iteration}\t{result.wall:.6f}\t{result.user:.6f}\t"
            f"{result.system:.6f}\t{result.maxrss}\t{result.db_bytes}\t{result.records}")

    print("\nmedian\twall_s\tuser_s\tsys_s\tmaxrss_kb\tdb_bytes\trecords")
    medians = {}
    for label in ("baseline", "candidate"):
        values = {
            "wall": result_median(results, label, "wall"),
            "user": result_median(results, label, "user"),
            "system": result_median(results, label, "system"),
            "maxrss": result_median(results, label, "maxrss"),
            "db_bytes": result_median(results, label, "db_bytes"),
            "records": result_median(results, label, "records"),
        }
        medians[label] = values
        print(f"{label}\t{values['wall']:.6f}\t{values['user']:.6f}\t{values['system']:.6f}\t"
            f"{format_integer_median(values['maxrss'])}\t{format_integer_median(values['db_bytes'])}\t"
            f"{format_integer_median(values['records'])}")

    print()
    for title, attribute in (
        ("wall time", "wall"),
        ("peak RSS", "maxrss"),
        ("database size", "db_bytes"),
    ):
        baseline = medians["baseline"][attribute]
        candidate = medians["candidate"][attribute]
        if baseline:
            print(f"{title} change: {(candidate - baseline) * 100 / baseline:+.2f}%")


def main():
    args = parse_args()
    baseline = executable(args.baseline, "baseline")
    candidate = executable(args.candidate, "candidate")
    if not Path("/usr/bin/time").is_file() or not os.access("/usr/bin/time", os.X_OK):
        fail("/usr/bin/time is required")

    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory() as temporary_name:
        temporary = Path(temporary_name)
        callgraph, shared_sources = create_fixtures(temporary)
        sources = [root / source for source in SOURCES]
        sources.extend((callgraph, *shared_sources))

        # Warm up the loader, Clang, and filesystem metadata caches.
        run_one(root, temporary, "warmup-baseline", baseline, 0, args.passes, sources)
        run_one(root, temporary, "warmup-candidate", candidate, 0, args.passes, sources)

        results = []
        for iteration in range(1, args.iterations + 1):
            order = (("baseline", baseline), ("candidate", candidate))
            if iteration % 2 == 0:
                order = tuple(reversed(order))
            for label, binary in order:
                results.append(run_one(root, temporary, label, binary, iteration, args.passes, sources))
        print_results(results)


if __name__ == "__main__":
    main()
