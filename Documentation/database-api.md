# Database API

The read-only database interface is declared in
`include/semindex_database.h` and provided by the
`semindex_database` library. It lets applications query a semindex SQLite
database without linking the Clang-based parser.

Open a database with `semindex_db_open()` and close it with
`semindex_db_close()`.

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

## Call queries

`semindex_db_query_calls()` returns direct caller or callee relationships
selected by `semindex_db_call_options_t`. Function names are not globally
unique when internal linkage is involved; set the stable `usr_id` when a
specific function has already been identified.

`semindex_db_query_functions()` resolves a set of variant-qualified stable
function IDs to their stored records. As with the CLI callgraph, indirect calls
are not resolved.
