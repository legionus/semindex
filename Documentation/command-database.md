# Compiler command database

`semindex compiler` stores its compiler arguments and `semindex index` stores
the command selected from `compile_commands.json`.  Both write to `commands.db`
beside the selected symbol database.  With the default paths, the state
directory contains:

```text
.semindex/semindex.db
.semindex/commands.db
```

Both indexing commands accept `--commands-database=PATH` to select another
command database and `--no-store-command` to disable command storage.  Commands
are keyed by index variant and canonical source path, so indexing the same
source again replaces its command only within that variant.

The database is deliberately separate from `semindex.db`.  Large compiler
argument vectors therefore do not enlarge the symbol lookup database or alter
its query plans.

## Layout

The `commands` table is a `WITHOUT ROWID` table with `(variant, file)` as its
primary key.  `directory` and `file` are stored as absolute paths.  `arguments`
is a BLOB containing the original argument vector separated by NUL bytes.  The
driver name is included as the first argument; when it was omitted from the
CLI, the stored driver is `cc`.

Arguments are kept as one opaque value instead of being normalized into string
and argument tables.  This makes each update a single UPSERT and avoids joins
and per-argument index maintenance on the compiler hot path.

The internal read API loads one command by `(variant, canonical file)` through
the table's primary key. It reconstructs only that command's argument vector;
language-server save handling does not scan or buffer the command database.

The command database uses WAL mode and `synchronous=OFF`.  Concurrent indexing
processes prepare their semantic records independently and hold the command
database write lock only for one row update.

## Export

Export the default `general` variant to a compilation database with:

```sh
semindex compile-commands -o compile_commands.json
```

Use `--variant=NAME` to export another variant and `--database=PATH` when the
command database is not `.semindex/commands.db`.  Without `--output`, JSON is
written to standard output.  Entries are ordered by canonical source path and
contain the standard `directory`, `file`, and `arguments` fields.

The exporter opens only the command database and does so in read-only mode.  It
does not query or modify the symbol database.
