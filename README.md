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

* LLVM/Clang 21 or newer
* CMake
* a compilation database (`compile_commands.json`)

Typical build:

```sh
mkdir build
cd build
cmake ..
make
```

The CLI can print either the default prototype output or a Sparse
`test-dissect`-style view:

```sh
semindex --format=dissect --compile-commands build/compile_commands.json path/to/file.c
```

## Current limitations

This is still a prototype.

Among the missing features are:

* complete type graph;
* typedef expansion;
* indirect call analysis;
* macro-aware indexing;
* incremental indexing;
* persistent symbol database.

## Motivation

The original Semantic Indexer for C (semind) was developed as part of the Sparse project. Since Sparse is no longer under active development, this project explores rebuilding the same idea on top of Clang while preserving a clean C API for applications using the indexer.
