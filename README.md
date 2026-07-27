# semindex

**semindex** (Semantic Indexer for C) is an experimental semantic indexer for the C programming language built on top of the Clang frontend.

The long-term goal of the project is to provide functionality similar to the original **semind** from the Sparse project while using an actively maintained parser and semantic analysis framework.

## Current status

This repository is an early prototype.

Implemented so far:

* parsing C translation units with Clang LibTooling and preprocessing `.S`
  sources;
* indexing declarations, definitions, and references for variables, fields,
  records, enums, enumerators, typedefs, functions, macros, and included files;
* access classification:

  * `READ`
  * `WRITE`
  * `ADDR`
  * `CALL`
* persistent SQLite storage with parallel writers, incremental file
  replacement, header fingerprints, and named index variants;
* separate compiler-command capture and `compile_commands.json` export;
* indexed search by qualified symbol name and access mode;
* direct caller and callee queries with stable function identities;
* LSP definition, reference, document-highlight, call-hierarchy, diagnostics,
  and saved-file update support;
* a C API exported from the C++ indexing implementation.

An indexing operation reports a clean, partial, or failed result. Partial
results contain records recovered by Clang despite frontend errors; failed
results contain no usable records. Callers of the parser API must inspect
`semindex_get_index_result()` instead of relying only on the integer return
value. The `compiler` and `index` commands store clean and partial results so
the persistent index describes the latest saved source. See
[Documentation/api.md](Documentation/api.md) for result semantics, diagnostics,
record lifetime, and fingerprints.

## Architecture

The project is split into two layers.

```
          +----------------------+
          |      C programs      |
          |  CLI / database / UI |
          +----------+-----------+
                     |
                 semindex.h
                     |
          +----------v-----------+
          |    libsemindex.so    |
          |                      |
          |  Clang LibTooling    |
          |  AST traversal       |
          |  semantic analysis   |
          +----------------------+
```

Only the parser implementation library is written in C++. Public parser and
database interfaces are exposed through plain C APIs; their C++ and internal
storage implementation symbols are hidden.

## Future work

The remaining long-term work is to deepen the semantic model beyond the
implemented symbol index, direct call graph, and SQLite queries. This includes:

* persistent, queryable type relationships;
* expanded typedef relationships;
* richer struct, union, and enum relationships;
* indirect-call target resolution;
* semantic queries beyond the original Sparse semind interface.

## Building

The project requires:

* LLVM/Clang 21 or newer;
* CMake;
* SQLite 3.35 or newer.

See [Documentation/building.md](Documentation/building.md) for Fedora and
Ubuntu package lists, versioned LLVM setup, tests, and troubleshooting.

Typical build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

A compilation database is required by `semindex index`; it is not required to
build semindex or to use `semindex compiler`.

The CLI prints a Sparse `test-dissect`-style view by default and can emit a
versioned JSON document for tooling:

```sh
semindex index --compile-commands build/compile_commands.json path/to/file.c
semindex index --format=json --compile-commands build/compile_commands.json path/to/file.c
```

The CLI can also index a single compile command directly:

```sh
semindex compiler -- -Iinclude -DDEBUG -c path/to/file.c -o file.o
```

After a clean frontend result, the indexing commands replace the source's
index records in `.semindex/semindex.db`. Partial or failed results make the
command fail without replacing the last clean records. `compiler` is quiet
unless `--format` is specified. For either indexing command, explicitly
selecting a format prints the in-memory index without creating either database.
Selected compiler arguments are stored
separately in `.semindex/commands.db`; use
`--no-store-command` to disable this. Local symbols and their uses are included
by default; use `--no-include-local` to omit them. A compiler name may be
provided after `--`; when omitted, `cc` is used as the Clang driver name.
When a `.git` marker is found above the source file, paths inside that
repository are stored relative to its root. The repository's current commit is
stored as provenance for the selected variant when semindex is built with
libgit2 and `--git-commit=auto` is requested. Such builds also accept an
explicit object ID through `--git-commit=COMMIT`. Provenance is disabled by
default to avoid libgit2 initialization overhead in every compiler process.

Export the commands captured for the default variant as a compilation
database:

```sh
semindex compile-commands -o compile_commands.json
```

Use `--variant=NAME` to export another indexed configuration.

Search by symbol name, including qualified structure fields:

```sh
semindex search task_struct.pid
```

Query direct call relationships in either direction:

```sh
semindex callgraph --callees=worker
semindex callgraph --callers=schedule
```

See [Documentation/callgraph.md](Documentation/callgraph.md) for function
identity, variant filtering, output, and indirect-call limitations.

Run the language server over standard input and output:

```sh
semindex lsp --database=.semindex/semindex.db
semindex-lsp --database=.semindex/semindex.db
```

See [Documentation/lsp.md](Documentation/lsp.md) for editor integration,
variants, saved-file updates, diagnostics, and navigation after parse errors.

The current database format is intentionally incompatible with earlier
prototype versions. Remove an old `.semindex/semindex.db` before rebuilding an
index.

## Current limitations

This is still a prototype.

Among the missing features are:

* a persistent type graph, including expanded typedef and complete type
  relationships;
* indirect-call target resolution and points-to analysis;
* indexing of code excluded by preprocessing; separate variants must be indexed
  explicitly to combine different configurations;
* queryable separation of header preprocessing contexts within one variant;
  their records are currently merged;
* assembly labels, directives, and references in `.S` files; only
  preprocessor-level entities are indexed;
* LSP support for unsaved buffers, completion, hover, rename, and workspace
  symbols.

## Motivation

The original Semantic Indexer for C (semind) was developed as part of the Sparse project. Since Sparse is no longer under active development, this project explores rebuilding the same idea on top of Clang while preserving a clean C API for applications using the indexer.
