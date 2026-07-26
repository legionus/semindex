# Refactoring performance results

This report records the performance check performed after completing the
structural refactoring roadmap.

## Environment

The comparison used:

* baseline semindex commit `a19c6446df5050627064f3cdcaa6649bf76cd48d`;
* candidate semindex commit `70a066ec10d88aa7dea2f38d371bb1fdcb508126`;
* Linux source revision `e8c2f9fdadee7cbc75134dc463c1e0d856d6e5c7`;
* `x86_64_defconfig`, configuration SHA-256
  `8d8883b0645ea30473c9756c74108041643bc8e910650c7fe9f510e4669c4ad1`;
* `make -j12 CC=clang-21 C=2`;
* AMD Ryzen 5 3600 with 12 logical CPUs and 78 GiB RAM;
* tmpfs build, trace, and database storage;
* Clang compiler 21.1.8 and Clang/LLVM libraries 22.1.2;
* SQLite 3.46.1.

Both runs used fresh out-of-tree kernel build directories and independent
symbol databases. Compiler command storage was disabled. The checker command
was:

```sh
semindex compiler --no-store-command --trace=TRACE \
	--database=STATE/semindex.db -- cc
```

The complete kernel build was run once per version because of its duration.
The repeated quick and search benchmarks below provide the regression check;
the full-build figures identify the profile distribution rather than claiming
a low-noise median.

## Kernel indexing

Both versions completed successfully without writer failures or
`database is locked` diagnostics. Each run indexed 2,961 translation units and
produced 7,802 file rows and 5,945,327 semantic records.

| Measurement | Baseline | Candidate | Change |
| --- | ---: | ---: | ---: |
| Complete kernel build wall time | 874.58 s | 849.95 s | -2.82% |
| Aggregate semindex process time | 5,719.41 s | 5,562.06 s | -2.75% |
| Parse | 4,511.53 s | 4,376.81 s | -2.99% |
| Fingerprint | 340.26 s | 331.07 s | -2.70% |
| Symbol database | 794.70 s | 786.61 s | -1.02% |
| Writer-lock acquisition | 64.01 s | 72.71 s | +13.59% |
| Symbol database size | 621,744,128 bytes | 620,974,080 bytes | -0.12% |
| Complete build peak RSS | 1,032,440 KiB | 1,032,388 KiB | -0.01% |

The candidate writer-lock increase is not a current bottleneck. Lock
acquisition accounts for 9.24% of symbol database time and 1.31% of aggregate
semindex time. Private staging remains the largest measured database phase.

The file fingerprint cache reused 93.78% of examined files in both runs.
Candidate private staging retained 9.09% of in-memory records, compared with
9.10% for the baseline. Both runs inserted the same 5,946,158 records during
their merge operations.

The repository quick benchmark was also run for five alternating iterations.
Its median changed by +0.65% wall time, -0.01% peak RSS, and 0.00% database
size. These differences are below the 5% regression threshold.

| Iteration | Baseline wall | Candidate wall | Baseline RSS | Candidate RSS |
| --- | ---: | ---: | ---: | ---: |
| 1 | 1.55 s | 1.56 s | 87,340 KiB | 87,324 KiB |
| 2 | 1.55 s | 1.55 s | 87,356 KiB | 87,348 KiB |
| 3 | 1.55 s | 1.55 s | 87,304 KiB | 87,364 KiB |
| 4 | 1.55 s | 1.56 s | 87,360 KiB | 87,356 KiB |
| 5 | 1.55 s | 1.57 s | 87,464 KiB | 87,292 KiB |

## Search and LSP

Exact search for `task_struct.pid` returned 135 rows from both databases.
Removing the Clang dependency from the query executable reduced median process
duration from 20.61 ms to 2.56 ms. Baseline measurements were 21.38, 20.61,
20.59, 20.50, and 21.14 ms; candidate measurements were 2.56, 2.20, 2.77,
2.24, and 2.80 ms.

A wildcard query over all 5,945,327 records had medians of 22.71 s for both
versions. Baseline measurements were 22.99, 22.31, 22.67, 22.74, and 22.71 s;
candidate measurements were 22.77, 22.72, 22.64, 23.00, and 22.64 s. The
unchanged result shows that broad search cost is dominated by streaming the
complete result set rather than process startup. The benchmark used an empty
output format so source-file I/O and terminal rendering were excluded.

Seven `textDocument/references` requests for `task_struct.pid` against one
running LSP process returned 133 unique locations. The first request took
19.79 ms. The complete measurements were 19.79, 17.42, 17.54, 16.68, 17.22,
16.72, and 16.37 ms, with a 17.22 ms median.

Five `textDocument/didSave` requests reindexed
`net/mac80211/airtime.c` using its stored kernel compile command. They took
5.74, 8.72, 4.58, 6.98, and 4.70 s, with a 5.74 s median. Save latency is
therefore dominated by re-running the Clang frontend for a large translation
unit, not by an interactive database query.

## CPU profile

A 99 Hz flat `perf record` profile of `net/mac80211/airtime.c` collected 510
samples without loss:

| Shared object | CPU samples |
| --- | ---: |
| `libclang-cpp.so.22.1` | 77.81% |
| `libc.so.6` | 10.88% |
| `libsemindex.so` | 5.60% |
| `libLLVM.so.22.1` | 1.46% |
| `semindex-compiler` | 0.84% |

The next optimization work should focus on reducing frontend and AST work,
especially for LSP save-time reindexing. Replacing SQLite, adding indexes, or
changing writer concurrency is not justified by this profile. A future
optimization should first add finer tracing around AST traversal and compare a
representative large translation unit before and after the change.
