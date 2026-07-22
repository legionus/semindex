# Index database

`semindex compiler` and `semindex index` store symbol records in
`.semindex/semindex.db` by default. The database contains only source files and
semantic records. Compiler commands are stored separately in
`.semindex/commands.db`. Local symbols and their uses are stored only when
indexing with `--include-local`.

## Layout

The `records` table stores declarations, definitions, and uses together. Its
`WITHOUT ROWID` primary key starts with the qualified symbol name, so an exact
query such as `task_struct.pid` is an indexed lookup rather than a scan.

Names and contexts are stored directly in each record. Types and general symbol
USRs remain available only from the in-memory index and output formats. Direct
call records store compact caller and callee IDs derived from their Clang USRs,
so static functions can be distinguished without repeating both identity
strings at every call site. IDs are computed before SQLite staging; adding a
record does not perform an identity lookup. A secondary index supports file
replacement.

A partial `(context, context_usr_id)` index contains only direct function-call
records and supports caller-to-callee queries. Callee-to-caller queries use the
records primary key, which already begins with the callee symbol. No callgraph
index is added to declarations, definitions, or non-call uses.

Each row in `files` is identified by `(variant, path)`.  Indexing commands use
the variant `general` by default and accept `--variant=NAME`.  Consequently,
the same physical source can have independent records for configurations such
as `x86-defconfig` and `arm64-defconfig` without repeating the variant string
in every symbol record.

## Concurrent writers

Each indexing process first inserts its records into private SQLite TEMP tables.
This work does not hold the shared database write lock. Once staging is complete,
the process performs one short bulk merge into the WAL database.

The main C source is replaced within its variant whenever its translation unit
is indexed. This removes references that changed because of compiler options
or included headers without affecting other variants of the same source.
Records from unchanged headers are merged with `INSERT OR IGNORE`, allowing
different translation units to contribute semantic results without physically
duplicating identical records. When a physical file's modification time or size
changes, its old records in the current variant are removed before merging the
new records.

For each file, the indexer computes a SHA-256 fingerprint from the semantic
records that would be stored in the database. Multiple fingerprints may belong
to one `(variant, path)` because preprocessing the same header under different
macro contexts can produce different records. If an unchanged included file
already has the same fingerprint, its records are omitted from private staging
and from the shared database merge. The main source file is never reused this
way because indexing it replaces its previous records.

A cache hit is checked again while holding the database writer lock. If another
writer invalidated it after private staging began, the process stages the full
record set and retries the merge. `INSERT OR IGNORE` remains the final guard for
writers that discover the same fingerprint concurrently.

The WAL database uses `synchronous=OFF`. An application or indexing-process
failure remains transaction-safe, but an operating-system crash can require
rebuilding the index. This is an intentional tradeoff for a reproducible cache.

## Compatibility

The database is an experimental interface. The current schema does not migrate
older prototype databases because dropping their tables would leave the original
multi-gigabyte file allocation in place. Remove an old database before indexing:

```sh
rm -f .semindex/semindex.db .semindex/semindex.db-shm \
	.semindex/semindex.db-wal .semindex/semindex.db-journal
```
