# Index management

The semindex databases are reproducible project state. They are designed for
incremental replacement and concurrent indexing, not as a substitute for the
source tree.

## State directory

Default paths are resolved below the nearest repository root identified by a
`.git` marker:

```text
.semindex/semindex.db
.semindex/commands.db
```

Outside a Git repository, the current directory is used. Indexing commands
accept `--root=DIR` when a release archive, generated tree, or unusual working
directory needs an explicit project root.

The symbol database stores semantic records and optional Git provenance. The
command database stores compiler argument vectors. Keep them separate: symbol
queries never need to load compiler commands.

## Update outcomes

Every frontend run has one of three outcomes:

* `clean`: indexing completed without frontend errors;
* `partial`: Clang recovered usable records despite errors;
* `failed`: no usable index was produced.

Clean and partial results replace records owned by the main source in the
selected variant. A failed result leaves those records unchanged. This policy
keeps the index aligned with saved source when recovery is possible without
discarding the last usable index after a complete frontend failure.

Included headers are shared by many translation units. Their records are
merged, and semantic fingerprints avoid restaging an unchanged header context.
See [index-database.md](index-database.md) for detailed replacement and
concurrency rules.

## Variants

The default variant is `general`. Use named variants for compiler
configurations that must coexist:

```sh
semindex compiler --variant=debug -- cc -DDEBUG -c source.c
semindex compiler --variant=release -- cc -DNDEBUG -c source.c
```

Reindexing a source replaces records only in the same variant. `semindex
search` and callgraph queries cover all variants unless restricted with
`--variant`. LSP and MCP may select one variant for a session.

Use separate databases instead of variants when configurations belong to
different source trees or should never be queried together.

## Refresh policy

semindex does not watch the complete source tree or compilation database.
Changing a header, generated source, compiler option, or
`compile_commands.json` does not automatically refresh every affected
translation unit.

Choose a refresh workflow appropriate for the build:

* rerun `semindex index` for affected entries in `compile_commands.json`;
* rebuild through `semindex cc` to capture the commands actually executed;
* open or save individual files through LSP for interactive updates.

LSP updates one document at a time. They do not replace a complete project
indexing pass after global build configuration changes.

## Compiler commands

Commands are keyed by canonical source path and variant. Reindexing that pair
replaces its saved command. Export one variant with:

```sh
semindex compile-commands --variant=general -o compile_commands.json
```

An explicit indexing output format is inspection-only and stores neither
symbols nor compiler commands. `--no-store-command` stores symbols without
updating `commands.db`.

See [command-database.md](command-database.md) for the command layout and
export contract.

## Repository provenance and source drift

Paths inside the selected repository root are stored relative to it. System
headers and other files outside the root remain absolute.

Git provenance is disabled by default. A Git-enabled build can record an
explicit commit or resolve the current commit with `--git-commit=auto`. The
commit describes the latest indexing update for a variant, not an atomic
snapshot of every record in that variant.

Source consumers compare stored file metadata with the working tree. When
provenance is available, they may retrieve drifted source from the recorded Git
commit. Source text itself is never copied into SQLite. See
[source-resolution.md](source-resolution.md).

## Rebuilding and compatibility

The symbol database schema is not migrated between incompatible prototype
versions. The command reports a schema mismatch instead of modifying an old
database in place.

Stop active indexers before deleting project state:

```sh
rm -rf .semindex
```

Then rerun the normal indexing workflow. Removing only `commands.db` preserves
symbol search but disables command-based LSP and MCP updates until commands are
captured again.

The symbol database uses WAL and is safe for parallel semindex writers. It is
configured as a rebuildable cache rather than for durability across an
operating-system crash. Do not treat copying a live database without its WAL
state as a backup procedure.
