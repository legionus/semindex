# semindex

**semindex** (Semantic Indexer for C and C++) is an experimental semantic
indexer for the C and C++ programming languages built on top of the Clang
frontend.

The long-term goal of the project is to provide functionality similar to the original **semind** from the Sparse project while using an actively maintained parser and semantic analysis framework.

## Current status

This repository is an early prototype.

Implemented so far:

* parsing C and C++ translation units with Clang LibTooling and preprocessing
  `.S` sources, including explicitly marked assembler functions and direct
  calls;
* indexing declarations, definitions, and references for variables, fields,
  records, classes, enums, enumerators, typedefs, functions (including C++
  member methods), macros, and included files;
* access classification:

  * `READ`
  * `WRITE`
  * `ADDR`
  * `CALL`
* persistent SQLite storage with parallel writers, incremental file
  replacement, header fingerprints, and named index variants;
* separate compiler-command capture and `compile_commands.json` export;
* transparent compiler wrapping for indexing during Make and CMake builds;
* indexed search by qualified symbol name and access mode;
* direct caller and callee queries with stable function identities;
* LSP definition, hover, reference, document-highlight, call-hierarchy,
  diagnostics, and saved-file update support;
* bounded MCP tools for semantic search, call relationships, source context,
  variants, index status, and opt-in single-file updates;
* a C API exported from the C++ indexing implementation.

An indexing operation reports a clean, partial, or failed result. Partial
results contain records recovered by Clang despite frontend errors; failed
results contain no usable records. Callers of the parser API must inspect
`semindex_get_index_result()` instead of relying only on the integer return
value. The `compiler`, `index`, and `cc` commands store clean and partial
results so the persistent index describes the latest saved source. See
[Documentation/api.md](Documentation/api.md) for result semantics, diagnostics,
record lifetime, and fingerprints.

## Architecture

semindex is organized as a set of command-line and protocol frontends over
shared indexing, query, and storage libraries.

```
 +-------------------------+       +-------------------------+
 | semindex dispatcher and |       | LSP and MCP servers     |
 | semindex-* commands     |       |                         |
 +------------+------------+       +------------+------------+
              |                                 |
              +---------------+-----------------+
                              |
              +---------------v-----------------+
              | indexing pipeline and semantic  |
              | query services                  |
              +---------+-------------+---------+
                        |             |
          +-------------v--+       +--v----------------------+
          | Clang parser,  |       | source and Git          |
          | AST, PP, and   |       | resolution              |
          | assembly index |       +-------------------------+
          +-------+--------+
                  |
       +----------v-----------+     +-------------------------+
       | symbol index         |     | compiler command        |
       | semindex.db          |     | commands.db             |
       +----------------------+     +-------------------------+
```

The `semindex` executable is a dispatcher for independently runnable
`semindex-*` tools. The parser, semantic query layer, LSP server, and MCP server
use C++ where Clang integration or owned protocol data benefits from it. CLI,
SQLite storage, and output code remain in C. Public parser and database
interfaces are plain C APIs, and implementation symbols are hidden.

Symbol records and compiler commands deliberately use separate SQLite
databases. The indexing pipeline coordinates parsing, fingerprints,
provenance, and persistence without making the parser depend on either CLI or
database policy. Protocol-neutral semantic and source-resolution libraries are
shared by LSP and MCP, while wire-format behavior remains in each server.

The README files under [`lib/`](lib/) describe the ownership boundary of each
internal library. Database layouts and update rules are documented in
[Documentation/index-database.md](Documentation/index-database.md) and
[Documentation/command-database.md](Documentation/command-database.md).

## Development direction

The immediate direction is to make the existing index useful as a dependable
foundation for editor and agent workflows. Work is focused on:

* broadening protocol-neutral semantic queries while keeping result sets
  bounded and indexed;
* improving incomplete-source and multi-configuration indexing without
  weakening replacement or provenance rules;
* extending type and call relationships where they can be represented without
  copying source text into the database;
* completing practical LSP and MCP workflows on top of the shared query layer.

Specific unsupported language and protocol features are listed in
[Current limitations](#current-limitations).

## Building

The project requires:

* LLVM/Clang 22 or newer;
* CMake;
* SQLite 3.35 or newer.

See [Documentation/building.md](Documentation/building.md) for Fedora and
Ubuntu package lists, versioned LLVM setup, tests, and troubleshooting.
New users can follow
[Documentation/getting-started.md](Documentation/getting-started.md) for a
complete indexing and query workflow.

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

To index while running the real compiler, use the transparent wrapper:

```sh
make CC=semindex-cc REAL_CC=cc
```

It can also be used as CMake's `CMAKE_C_COMPILER_LAUNCHER`. See
[Documentation/compiler-wrapper.md](Documentation/compiler-wrapper.md) for
pass-through behavior, options, and indexing error policies.

After a clean or partial frontend result, the indexing commands replace the
source's index records in `.semindex/semindex.db`. A failed result contains no
usable records and does not replace the stored index. `compiler` is quiet
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

Run the read-only MCP server with an explicit source-access boundary:

```sh
semindex mcp --database=.semindex/semindex.db --workspace="$PWD"
```

Add `--allow-reindex` to expose controlled single-file updates through saved
compiler commands. Full-project indexing and client-supplied compiler arguments
remain unavailable through MCP.

See [Documentation/mcp.md](Documentation/mcp.md) for the tool set, pagination,
source limits, protocol version, and security boundary.

## Documentation

User guides:

* [Getting started](Documentation/getting-started.md);
* [Command reference](Documentation/command-reference.md);
* [Index management](Documentation/index-management.md);
* [Building and installation](Documentation/building.md);
* [LSP editor integration](Documentation/lsp-editors.md).

Interface and implementation details:

* [Parser API](Documentation/api.md) and
  [database API](Documentation/database-api.md);
* [symbol index](Documentation/index-database.md) and
  [compiler command database](Documentation/command-database.md);
* [output formats](Documentation/output-formats.md),
  [callgraph](Documentation/callgraph.md), and
  [source resolution](Documentation/source-resolution.md);
* [LSP](Documentation/lsp.md) and [MCP](Documentation/mcp.md).

The current database format is intentionally incompatible with earlier
prototype versions. Remove an old `.semindex/semindex.db` before rebuilding an
index.

## Current limitations

This is still a prototype.

Among the missing features are:

* a persistent type graph beyond the stored declared-to-canonical type
  relationships;
* indirect-call target resolution and points-to analysis;
* indexing of code excluded by preprocessing; separate variants must be indexed
  explicitly to combine different configurations;
* queryable separation of header preprocessing contexts within one variant;
  their records are currently merged;
* general assembly labels, directives, and references in `.S` files; functions
  marked with `.type` or `.ent` and common direct-call instructions are
  recognized, but indirect calls and assembler `.macro` expansion are not;
* LSP support for unsaved buffers, completion, rename, and workspace symbols;
* C++ lambdas and multiple inheritance; templates have no dedicated template
  symbol (the primary template is indexed as a regular class, and
  specializations are not recorded separately; see
  [Documentation/api.md](Documentation/api.md)); `new`/`delete`
  expressions and virtual-dispatch calls are not recorded as references.

## Motivation

The original Semantic Indexer for C (semind) was developed as part of the Sparse project. Since Sparse is no longer under active development, this project explores rebuilding the same idea on top of Clang while preserving a clean C API for applications using the indexer.
