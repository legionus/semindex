# Index database

`semindex compiler` and `semindex index` store symbol records in
`.semindex/semindex.db` by default. The database contains only source files,
semantic records, and optional repository provenance. Compiler commands are
stored separately in `.semindex/commands.db`. Local symbols and their uses are
stored by default; indexing with `--no-include-local` omits them.

An explicit `--format=FORMAT` selects output-only inspection mode for either
indexing command and does not create or update either database.

## Layout

The `records` table stores declarations, definitions, and uses together. Its
`WITHOUT ROWID` primary key starts with the qualified symbol name, so an exact
query such as `task_struct.pid` is an indexed lookup rather than a scan.

Names and contexts are stored directly in each record. Types and general symbol
USRs remain available only from the in-memory index and output formats.
Function symbols and direct call records store compact IDs derived from their
Clang USRs, so static functions can be distinguished without repeating identity
strings at every call site. IDs are computed before SQLite staging; adding a
record does not perform an identity lookup. A secondary file index supports file
replacement and narrows source-position lookup to one indexed file.

## Reader API

`libsemindex_database` exposes a read-only C API in
`include/semindex_database.h`. A caller keeps an opaque database handle open
and receives matching declarations, definitions, and references through a
callback. Record strings point into the active SQLite row and remain valid only
until that callback returns, so large searches do not require a buffered copy
of the result set.

Name queries accept the same exact and glob patterns as `semindex search`.
They may also restrict local records by their containing function and local
status, allowing language-server queries to distinguish same-named local
variables without adding an index or changing the database layout.
Record queries have a stable total order and accept a result limit plus an
optional cursor. Pagination compares the complete ordered record key rather
than using `OFFSET`, so later pages do not scan and discard all earlier
results. A caller copies the last returned record into a cursor before its
callback returns; the next query resumes strictly after that record. A zero
limit preserves the unbounded streaming behavior used by existing callers.
Position queries use one-based source byte coordinates and may select one
variant or return matches from all variants. The LSP layer is responsible for
converting its UTF-16 document positions before calling this API.

A partial `(context, context_usr_id)` index contains only direct function-call
records and supports caller-to-callee queries. Callee-to-caller queries use the
records primary key, which already begins with the callee symbol. No callgraph
index is added to declarations, definitions, or non-call uses. The reader API
exposes both directions as streamed call records and can filter function symbol
queries by their stable ID.

Each row in `files` is identified by `(variant, path)`.  Indexing commands use
the variant `general` by default and accept `--variant=NAME`.  Consequently,
the same physical source can have independent records for configurations such
as `x86-defconfig` and `arm64-defconfig` without repeating the variant string
in every symbol record.

The indexer searches upward from the main source file for a `.git` directory or
file. When found, source and header paths inside that repository are stored
relative to its root. Files outside the repository, including system headers,
are stored as canonical absolute paths. Path conversion is performed once per
indexed file rather than once per semantic record.

The `variants` table lists every indexed variant and records its repository
root when one is available. It also records optional source provenance when
semindex is built with libgit2. Use `--git-commit=auto` to resolve the repository's current
`HEAD`, or `--git-commit=COMMIT` to record a known 40- or 64-digit object ID
explicitly. Git provenance is disabled by default so short-lived compiler
wrapper processes do not pay the libgit2 repository initialization cost. A
source outside a Git repository is indexed normally with `NULL` repository and
commit fields. `--no-git-commit` explicitly disables provenance.

libgit2 is isolated in an optional runtime-loaded backend. Normal indexing and
explicit object IDs do not load it; only `--git-commit=auto` opens a repository
through libgit2. This keeps the compiler-wrapper path independent of libgit2's
initialization cost unless automatic discovery was requested.

Metadata is one row per variant rather than one value per file or record. It
therefore adds constant database space and one conditional upsert to the
existing writer transaction. The row describes the commit seen by the most
recent successful indexing command that recorded provenance for that variant.
Incremental indexing is not an atomic repository snapshot: if `HEAD` changes
between translation units, existing records may still have been produced from
the earlier commit. Consumers must treat a mismatch between the stored commit
and the current worktree as possible source/index drift.

The public database reader exposes this metadata through the streaming
`semindex_db_list_variants()` API. Variants are returned in stable name order;
the repository root and Git commit are `NULL` when unavailable.

A symbol database is therefore intended to describe one repository. Relative
paths are resolved by consumers against that repository root; the LSP uses the
workspace root supplied by the client.

## Concurrent writers

Each indexing process first inserts its records into private SQLite TEMP tables.
This work does not hold the shared database write lock. Once staging is complete,
the process performs one short bulk merge into the WAL database.

The main C source is replaced within its variant whenever the frontend produces
index data. This includes partial results recovered from a source file with
syntax or semantic errors, so the database continues to describe the saved
file instead of retaining a stale clean index. A failed frontend run that
produces no index data leaves the previous records unchanged. This removes
references that changed because of compiler options or included headers without
affecting other variants of the same source.
Records from unchanged headers are merged with `INSERT OR IGNORE`, allowing
different translation units to contribute semantic results without physically
duplicating identical records. When a physical file's modification time or size
changes, its old records in the current variant are removed before merging the
new records.

For each file, the indexer computes a BLAKE3 fingerprint from the semantic
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

The database is an experimental interface. Schema version 12 lists variants
even when Git provenance is disabled and does not migrate older databases. Dropping old
tables in place would also leave their original multi-gigabyte file allocation.
Remove an old database before indexing:

```sh
rm -f .semindex/semindex.db .semindex/semindex.db-shm \
	.semindex/semindex.db-wal .semindex/semindex.db-journal
```
