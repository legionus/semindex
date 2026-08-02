# Database API

The read-only database interface is declared in
`include/semindex_database.h` and provided by the
`libsemindex_database.so` library. It lets applications query a semindex SQLite
database without linking the Clang-based parser.

This library and `libsemindex.so` are the project's public libraries. Their
installed headers are `semindex_database.h` and `semindex.h`, respectively.
The storage, query-adapter, and indexing-pipeline libraries are internal static
implementation details; they are neither installed nor covered by the public
ABI. `libsemindex_database.so` depends on SQLite, but not on Clang or LLVM.

Open a database with `semindex_db_open()` and close it with
`semindex_db_close()`. `semindex_db_interrupt()` may be called from another
thread to stop the operation currently running on a database handle. The
caller must still keep the handle alive until the interrupted operation has
returned.

## Record queries

Fill `semindex_db_query_options_t` and pass it to
`semindex_db_query()`. A query can filter by:

* qualified symbol name;
* path and variant;
* containing function;
* declaration, definition, or reference record;
* access mode, symbol kind, stable function ID, and local-symbol status.

Optional scalar filters are enabled by their corresponding `has_*` member.
Strings in each `semindex_db_record_t` are valid only until the callback
returns. A nonzero callback result stops iteration and is returned to the
caller.

`semindex_db_find_at()` finds records covering a one-based source byte
position. The path and variant must identify the same values stored in the
index.

## Declared type queries

`semindex_db_query_symbol_types()` streams declared and canonical C types for an
exact `semindex_db_identity_t`. Results are ordered by declared type, canonical
type, and source path. The query accepts a limit and a caller-owned cursor so
clients can paginate without using `OFFSET`. Type strings remain valid only until the callback
returns.

## Call queries

`semindex_db_query_calls()` returns direct caller or callee relationships
selected by `semindex_db_call_options_t`. Function names are not globally
unique when internal linkage is involved; set the stable `usr_id` when a
specific function has already been identified.

`semindex_db_query_functions()` resolves a set of variant-qualified stable
function IDs to their stored records. As with the CLI callgraph, indirect calls
are not resolved.
