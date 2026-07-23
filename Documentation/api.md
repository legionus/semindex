# Parser API

The public parser interface is declared in `include/semindex.h` and exported by
`libsemindex.so`. It is a C interface and can be used from both C and C++.

## Lifetime

Create one context with `semindex_create()` and release it with
`semindex_destroy()`. Configuration setters apply to subsequent index
operations.

Pointers returned by getters belong to the context. They remain valid until the
next index operation on that context or until `semindex_destroy()`. Callers
must copy data that must outlive either event. Indexing the same context
concurrently is not supported; use a separate context in each worker.

## Indexing

`semindex_index_file()` selects the source's command from a compilation
database. Its first argument may name either `compile_commands.json` or the
directory containing it.

`semindex_index_command()` accepts an explicit
`semindex_compile_command_t`. The argument vector has the same meaning as a
compiler invocation and must contain the source file.

After either operation, inspect `semindex_get_index_result()`:

* `SEMINDEX_INDEX_CLEAN` means the frontend completed without errors;
* `SEMINDEX_INDEX_PARTIAL` means Clang recovered usable records despite
  errors;
* `SEMINDEX_INDEX_FAILED` means no usable records were produced.

The integer return value reports the frontend invocation result and does not
replace the status check. In particular, partial results can contain symbols
and uses even when the operation returns a failure.

Diagnostics are available through `semindex_diagnostic_count()` and
`semindex_get_diagnostic()`. Locations are one-based when present; a
diagnostic without a source location has line and column zero.

## Records

Use `semindex_symbol_count()` and `semindex_get_symbol()` to enumerate
declarations and definitions. Uses are available through
`semindex_use_count()` and `semindex_get_use()`.

`semindex_use_t.kind` gives the broad operation (`READ`, `WRITE`,
`ADDR`, or `CALL`). Its `mode` field is the more detailed
`semindex_use_mode_t` bitmask for address, value, and pointer access.

Local symbols are included by default. Disable them before indexing with
`semindex_set_include_local(s, 0)`. The source scope is selected with
`semindex_set_scope()`.

## File fingerprints

Call `semindex_build_file_fingerprints()` after a clean index operation to
derive fingerprints for indexed files. Enumerate them with
`semindex_file_fingerprint_count()` and
`semindex_get_file_fingerprint()`. A fingerprint includes the semantic
records attributed to the file and can be used by storage implementations to
avoid replacing unchanged header records.
