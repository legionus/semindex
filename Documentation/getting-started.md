# Getting started

This guide covers the shortest path from a source tree to a searchable
semantic index. See [building.md](building.md) for dependencies, distribution
packages, installation, and development checks.

## Build or install semindex

Build semindex from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
export PATH="$PWD/build:$PATH"
```

Alternatively, install the build and use the installed commands:

```sh
cmake --install build
```

Verify that the dispatcher can find its command helpers:

```sh
semindex --help
semindex search --help
```

## Choose an indexing workflow

semindex has three indexing entry points. They produce the same symbol index
but obtain compiler arguments differently.

### Existing compilation database

Use `semindex index` when the project already has `compile_commands.json`:

```sh
semindex index \
    --compile-commands=build/compile_commands.json \
    path/to/source.c
```

The command indexes one translation unit. Run it once for every source that
should contribute to the index. CMake can generate the compilation database
with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.

### Explicit compiler arguments

Use `semindex compiler` when compiler arguments are already available:

```sh
semindex compiler -- cc -Iinclude -DDEBUG -c path/to/source.c
```

The compiler name is optional and defaults to `cc`. This command indexes the
source but does not run the real compiler.

### Index while building

Use the transparent compiler wrapper to index every eligible compilation while
the normal build runs:

```sh
make CC=semindex-cc REAL_CC=cc
```

For CMake, configure it as a compiler launcher:

```sh
cmake -S . -B build \
    -DCMAKE_C_COMPILER_LAUNCHER=semindex-cc
cmake --build build
```

The wrapper executes the real compiler after indexing. See
[compiler-wrapper.md](compiler-wrapper.md) for pass-through rules and error
policies.

## Inspect the index

The default state directory is created below the detected repository root:

```text
.semindex/semindex.db
.semindex/commands.db
```

Search declarations, definitions, and references by qualified symbol name:

```sh
semindex search task_struct.pid
semindex search --mode=w task_struct.pid
semindex search 'worker*'
```

C++ members are searched the same way, qualified by their class:

```sh
semindex search Circle.process
semindex search --kind=function 'Circle.*'
```

Namespaced C++ members use the `::`-qualified form:

```sh
semindex search alpha::Point.process
```

For the full list of supported and unsupported C++ features, see
[C++ support](cpp-support.md).

Query direct call relationships:

```sh
semindex callgraph --callees=worker
semindex callgraph --callers=schedule
```

See [command-reference.md](command-reference.md),
[output-formats.md](output-formats.md), and [callgraph.md](callgraph.md) for
filters and output details.

## Start an editor or agent server

Start the LSP server through the dispatcher:

```sh
semindex lsp
```

The server uses an existing index when available and indexes opened and saved
files incrementally. It can start with an empty database. See [lsp.md](lsp.md)
and [lsp-editors.md](lsp-editors.md).

Start the MCP server with an explicit source-access boundary:

```sh
semindex mcp --workspace="$PWD"
```

MCP queries are read-only by default. See [mcp.md](mcp.md) before enabling
controlled single-file updates.

## Rebuild or remove an index

Reindexing the same source and variant replaces records owned by that source.
It does not automatically reindex other translation units after build flags or
headers change. Run the selected project indexing workflow again for a complete
refresh.

The databases are reproducible caches. To start over, stop active semindex
processes and remove the state directory:

```sh
rm -rf .semindex
```

See [index-management.md](index-management.md) before combining variants,
moving databases, or automating index refreshes.
