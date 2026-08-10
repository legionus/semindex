# Command reference

The `semindex` executable dispatches subcommands to independently runnable
`semindex-*` helpers. Both forms are equivalent:

```sh
semindex search SYMBOL
semindex-search SYMBOL
```

Use `semindex COMMAND --help` for the complete option list. This document
describes each command's role, inputs, and persistent effects.

## index

```sh
semindex index [OPTION]... SOURCE
```

Indexes one `.c` or preprocessed `.S` source using the command selected from a
compilation database. `--compile-commands=PATH` accepts either
`compile_commands.json` or its directory. The default is the current directory.

Clean and partial results update the symbol database and, unless
`--no-store-command` is used, the command database. `--format=dissect` or
`--format=json` selects output-only mode and updates neither database.

## compiler

```sh
semindex compiler [OPTION]... -- [COMPILER] ARG...
```

Indexes exactly one `.c` or `.S` source from an explicit compiler argument
vector. It does not execute the compiler. The driver is optional and defaults
to `cc`.

The command has the same storage, scope, variant, provenance, tracing, and
output-only behavior as `index`.

## cc

```sh
semindex cc [OPTION]... -- [COMPILER] ARG...
```

Acts as a transparent compiler wrapper. Eligible one-source compilations are
indexed, then the process is replaced by the real compiler. Unsupported
compiler invocations are passed through without indexing.

`REAL_CC` selects the compiler when it is omitted from the command line.
`--index-errors=warn|fail|ignore` controls whether an indexing failure is
reported, prevents compilation, or is suppressed. See
[compiler-wrapper.md](compiler-wrapper.md).

## search

```sh
semindex search [OPTION]... [PATTERN]
```

Streams matching declarations, definitions, and references from the symbol
database. A pattern containing `*`, `?`, or `[]` uses SQLite GLOB syntax;
otherwise matching is exact.

Results can be filtered by path, variant, record type, symbol kind, and access
mode. `--format=STRING` controls the fields printed for each row. See
[output-formats.md](output-formats.md).

## callgraph

```sh
semindex callgraph (--callers=FUNCTION | --callees=FUNCTION) [OPTION]...
```

Queries direct caller or callee relationships. Variant and path filters may be
exact or GLOB patterns. `--show-id` prints stable function identities, and
`--id=ID` disambiguates same-named functions. See [callgraph.md](callgraph.md).

## compile-commands

```sh
semindex compile-commands [OPTION]...
```

Exports one variant from `commands.db` as `compile_commands.json`. Output goes
to standard output unless `--output=PATH` is provided. This command reads only
the compiler command database and does not modify the symbol index.

## lsp

```sh
semindex lsp [OPTION]...
```

Runs the Language Server Protocol server over standard input and output. It
supports definition, references, document highlights, direct-call hierarchy,
diagnostics, and indexing on document open and save.

The server uses saved commands, an explicitly selected compilation database,
or `compile_commands.json` in the repository root. If none is available, it
uses a transient approximate index for the opened file. See [lsp.md](lsp.md).

## mcp

```sh
semindex mcp [OPTION]...
```

Runs bounded Model Context Protocol tools over standard input and output.
`--workspace=PATH` defines the source-access boundary. Tools are read-only by
default; `--allow-reindex` enables a controlled update tool that can use only a
previously stored compiler command. See [mcp.md](mcp.md).

## Common indexing options

The `index`, `compiler`, and `cc` commands share these concepts:

* `--database=PATH` selects the symbol database;
* `--commands-database=PATH` selects the compiler command database;
* `--variant=NAME` selects an independently replaceable configuration;
* `--root=DIR` selects the root used for stored relative paths;
* `--scope=file|project|all` controls which parsed files contribute records;
* `--no-include-local` omits local symbols and uses;
* `--no-store-command` disables compiler command capture;
* `--trace=FILE` appends performance trace events.

Git-enabled builds also provide `--git-commit=COMMIT` and
`--no-git-commit`. Automatic provenance is opt-in because opening a repository
through libgit2 adds work to every indexing process.
