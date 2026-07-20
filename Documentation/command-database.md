# Compiler command database

`semindex compiler` stores the compiler command for each indexed translation
unit in `commands.db` beside the selected symbol database.  With the default
paths, the state directory contains:

```text
.semindex/semindex.db
.semindex/commands.db
```

Use `--commands-database=PATH` to select another command database or
`--no-store-command` to disable command storage.  Commands are keyed by index
variant and canonical source path, so indexing the same source again replaces
its command only within that variant.

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

The command database uses WAL mode and `synchronous=OFF`.  Concurrent compiler
processes prepare their semantic records independently and hold the command
database write lock only for one row update.
