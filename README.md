# semindex

**semindex** (Semantic Indexer for C) is an experimental semantic indexer for the C programming language built on top of the Clang frontend.

The long-term goal of the project is to provide functionality similar to the original **semind** from the Sparse project while using an actively maintained parser and semantic analysis framework.

## Current status

This repository is an early prototype.

Implemented so far:

* parsing C translation units using Clang LibTooling;
* indexing of:

  * global and local variables;
  * structure and union fields;
  * function definitions;
* generation of stable symbol identifiers (Clang USR);
* indexing of symbol uses;
* basic use classification:

  * `READ`
  * `WRITE`
  * `ADDR`
  * `CALL`
* C API exported from a C++ implementation, allowing the rest of the project to remain in C.
* persistent SQLite symbol database;
* indexed search by qualified symbol name;

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

Only the implementation library is written in C++. All public interfaces are exposed through a plain C API.

## Project goals

The intended capabilities include:

* semantic symbol database;
* indexing of declarations and references;
* accurate type information;
* struct/union/enum relationships;
* typedef resolution;
* function call graph;
* SQLite backend;
* semantic search similar to the original Sparse semind.

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

The CLI can print either the default prototype output or a Sparse
`test-dissect`-style view:

```sh
semindex index --format=dissect --compile-commands build/compile_commands.json path/to/file.c
```

The CLI can also index a single compile command directly:

```sh
semindex compiler -- -Iinclude -DDEBUG -c path/to/file.c -o file.o
```

The indexing commands write index records to `.semindex/semindex.db` and
`compiler` is quiet unless `--format` is specified. Their selected compiler
arguments are stored separately in `.semindex/commands.db`; use
`--no-store-command` to disable this. Local
symbols and their uses are omitted from the persistent index unless
`--include-local` is specified. A compiler name may be provided after `--`;
when omitted, `cc` is used as the Clang driver name.

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

The current database format is intentionally incompatible with earlier
prototype versions. Remove an old `.semindex/semindex.db` before rebuilding an
index.

## Current limitations

This is still a prototype.

Among the missing features are:

* complete type graph;
* typedef expansion;
* indirect call analysis;
* macro-aware indexing;
* incremental indexing;
* context-sensitive separation of header variants.

## Motivation

The original Semantic Indexer for C (semind) was developed as part of the Sparse project. Since Sparse is no longer under active development, this project explores rebuilding the same idea on top of Clang while preserving a clean C API for applications using the indexer.
