# Performance testing

Performance is part of semindex correctness.  The intended workloads contain
millions of symbol records, repeated header parsing, concurrent database
writers, and interactive search queries.  A change that is insignificant on a
single test file can make indexing a Linux kernel tree unusable.

## When measurements are required

Measure performance before and after changes to:

* AST or preprocessor traversal;
* source location and record construction;
* SQLite schemas, indexes, queries, transactions, or PRAGMAs;
* incremental file replacement and header handling;
* compiler command capture;
* result sorting, buffering, or output formatting;
* synchronization or parallel writer behavior.

Do not infer performance from code shape alone.  Profile after reproducing the
regression, and optimize the measured hot path.

## Build configuration

Use equivalent `RelWithDebInfo` builds for the baseline and candidate:

```sh
cmake -S /path/to/baseline -B /path/to/baseline/build \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /path/to/baseline/build -j"$(nproc)"

cmake -S /path/to/candidate -B /path/to/candidate/build \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /path/to/candidate/build -j"$(nproc)"
```

Both builds must use the same LLVM, Clang, SQLite, compiler, and linker
versions.  Record `llvm-config --version` and the system hardware when
publishing results.

Build the baseline from the commit immediately before the change unless the
comparison has another explicitly stated base.  Use a separate worktree rather
than checking out another commit over a dirty development tree:

```sh
git worktree add --detach /tmp/semindex-baseline BASELINE_COMMIT
cmake -S /tmp/semindex-baseline -B /tmp/semindex-baseline/build \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/semindex-baseline/build -j"$(nproc)"
```

Record the baseline and candidate commit IDs with the results.  Never compare
against an unidentified or stale executable left by an older build.

## Quick A/B benchmark

The repository benchmark indexes a fixed set of test translation units into a
new symbol and command database for every measurement.  By default, each
measurement makes five passes over the source set so that timer resolution and
process startup noise do not dominate the result.  Later passes also exercise
incremental replacement of previously indexed files.

The source set includes a generated translation unit with 256 direct call
edges.  This keeps database-size and indexing-time measurements sensitive to
callgraph storage without committing a large generated fixture.

```sh
scripts/benchmark.sh \
    --baseline=/path/to/baseline/build/semindex \
    --candidate=build/semindex \
    --iterations=5
```

Use `--passes=N` only when changing the measured batch duration.  Keep both
builds on the same iteration and pass counts.

The script alternates baseline and candidate runs and reports:

* wall, user, and system time;
* maximum resident set size;
* total SQLite database size;
* stored record count.

Use at least five measured iterations.  The quick benchmark is a regression
screen, not a substitute for the kernel benchmark when a change affects a hot
path or database design.

## Measurement rules

* Use an otherwise idle machine with a fixed CPU power policy when possible.
* Use the same source tree, configuration, command line, storage, and `-j`
  value for baseline and candidate.
* Give every measured indexing run a new database directory.
* Perform a warm-up before collecting measurements.
* Alternate baseline and candidate runs to reduce time-dependent bias.
* Report the median of at least five runs, together with the raw results.
* Keep warm-cache and cold-cache results separate.  Do not compare one against
  the other.
* Redirect search and indexing output identically in both runs.
* Record failures, skipped translation units, and `database is locked` errors;
  do not report a failed run as a performance result.

A median regression greater than 5 percent in wall time, peak RSS, database
size, or exact-search latency requires an explanation and explicit approval.
Results close to this boundary should be repeated with more iterations and the
kernel workload.

## Linux kernel benchmark

Use the same Linux commit and `.config` for both builds.  Keep benchmark state
outside the source tree and use separate kernel output and semindex database
directories.  Record the complete command line, LLVM version, CPU model,
memory size, filesystem, and parallelism.

The checker invocation has this form:

```sh
/usr/bin/time -v -o baseline.time \
    make O="$BASELINE_KBUILD" -j12 CC=clang C=2 \
    CHECK="$BASELINE_SEMINDEX compiler \
        --database=$BASELINE_STATE/semindex.db \
        --commands-database=$BASELINE_STATE/commands.db -- cc"
```

Run the candidate with equivalent kernel output and state directories.  Ensure
that both output directories start from equivalent configurations and build
states.  Alternate the order of complete baseline and candidate runs when more
than one repetition is practical.

At minimum, report:

* `real`, `user`, `sys`, and maximum RSS from `/usr/bin/time -v`;
* the number of indexed files and records;
* the sizes of `semindex.db`, `commands.db`, and any WAL files;
* all diagnostics and failed translation units;
* exact and wildcard search timings;
* whether any parallel writer failed or reported a locked database.

Do not reuse a normal development index for a benchmark.  Delete only the
dedicated benchmark state directory between runs.

## Search measurements

Measure an exact field lookup and a broad result scan against the same completed
database.  Redirect output so terminal rendering is not included:

```sh
perf stat -r 5 -- semindex search \
    --database="$STATE/semindex.db" task_struct.pid >/dev/null
perf stat -r 5 -- semindex search \
    --database="$STATE/semindex.db" '*' >/dev/null
```

For SQL changes, inspect the query plan.  Exact symbol lookup must not scan the
complete records table.  Add a plan assertion to the test suite when a query
depends on a specific index.

Record the result count as well as latency.  A faster query that omits matches
is a correctness failure.

## Profiling tools

Use `perf stat` to separate CPU time, instructions, cache behavior, and context
switching:

```sh
perf stat -r 5 -d -- command arguments
```

Use sampling to locate CPU hot paths:

```sh
perf record -g --call-graph dwarf -- command arguments
perf report
```

Use `/usr/bin/time -v` for peak RSS and process-level I/O statistics.  Use
Valgrind Massif or Callgrind only with a small translation-unit set because
running Clang under Valgrind is prohibitively slow for a full kernel tree.

For SQLite size analysis, record both logical counts and physical size:

```sh
sqlite3 .semindex/semindex.db \
    'SELECT count(*) FROM files; SELECT count(*) FROM records;'
sqlite3 .semindex/semindex.db 'PRAGMA page_count; PRAGMA page_size;'
stat -c '%n %s' .semindex/*.db .semindex/*.db-wal 2>/dev/null
```

Do not introduce a new profiling dependency silently.  If a required tool is
missing, ask for it to be installed before drawing conclusions.
