# Compiler wrapper

`semindex cc` and its direct helper `semindex-cc` provide transparent indexing
while a build runs its real C compiler. Unsupported invocations are passed to
the compiler without indexing, which allows one wrapper to handle compiler
probes, linking, preprocessing, dependency generation, response files, and
ordinary one-source compilations.

The existing `semindex compiler` command remains a strict indexing command. It
does not run a compiler and reports unsupported argument vectors as errors.

## Make compiler

Set `REAL_CC` to one executable and use `semindex-cc` as the compiler:

```sh
make CC=semindex-cc REAL_CC=cc
```

`REAL_CC` is not parsed by a shell. Use a wrapper script when the real compiler
requires a fixed prefix or multiple words. If `REAL_CC` is unset, `cc` is used.

## CMake launcher

CMake compiler launchers pass the selected compiler as the first argument:

```sh
cmake -S . -B build \
	-DCMAKE_C_COMPILER_LAUNCHER=/path/to/semindex-cc
cmake --build build
```

The launcher form does not require `REAL_CC`. Compiler arguments are forwarded
unchanged, and the compiler's exit status becomes the wrapper's exit status.

## Options

Wrapper options must precede an explicit `--`:

```sh
semindex cc --database=/path/to/semindex.db --variant=debug -- \
	cc -Iinclude -DDEBUG -c source.c -o source.o
```

Raw invocations without `--` are interpreted as compiler arguments. This keeps
compiler options from being consumed by the wrapper. Build-system integration
may configure `SEMINDEX_DATABASE`, `SEMINDEX_COMMANDS_DATABASE`,
`SEMINDEX_VARIANT`, and `SEMINDEX_INDEX_ERRORS`; `REAL_CC` selects the compiler
executable.

By default, an indexing failure is reported and compilation continues. Select
another policy with `--index-errors=warn|fail|ignore`. `fail` prevents the real
compiler from running after an indexing failure. `ignore` suppresses indexing
diagnostics and continues. A compiler failure is never hidden by these modes.

Only commands with exactly one `.c` or `.S` source are indexed. Preprocess-only,
assembly-output, dependency-only, multiple-source, response-file, compiler
probe, and link-only invocations are passed through. The exact real compiler
argument vector, including its driver name, is stored in `commands.db` for
indexed commands.

The wrapper sets `SEMINDEX_CC_ACTIVE` before executing the real compiler and
rejects an already guarded invocation. This prevents accidental recursion when
`REAL_CC` or a launcher points back to `semindex-cc`.
