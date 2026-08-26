# Parser API

The public parser interface is declared in `include/semindex.h` and exported by
`libsemindex.so`. It is a C interface and can be used from both C and C++.
Only declarations marked with `SEMINDEX_API` are part of the exported ABI.
The library's C++ implementation symbols are hidden and are not an interface
for applications.

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

Missing include files are reported as errors but do not stop preprocessing.
Clang continues parsing the main file and produces a partial index for
constructs whose declarations and types remain available. Semindex does not
invent declarations for content that would have come from the missing header.

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

Symbol kinds cover C and C++ sources. C++ classes and structs are symbols of
kind `SEMINDEX_SYMBOL_CLASS`; their member functions are `SEMINDEX_SYMBOL_FUNCTION`
symbols qualified by the class name, and members are `SEMINDEX_SYMBOL_FIELD`
symbols. Owner-qualified names use the `.owner.name` form (for example
`Circle.process`), the same convention as ordinary structure fields. Namespaced
C++ members use the C++ spelling `ns::Circle.member` instead. Two
methods with the same name in different classes have distinct USRs and thus
distinct `usr_id` values.

C++ templates currently produce no dedicated "template" symbol. The primary
template record is indexed once, as a regular `SEMINDEX_SYMBOL_CLASS` (with its
members as the usual `SEMINDEX_SYMBOL_FUNCTION` / `SEMINDEX_SYMBOL_FIELD`
records); the template nature is observable only through the USR prefix
(`ST>`) and the placeholder spellings in member types (for example the
parameter type `T` in `long (T)`). Specializations and explicit
instantiations such as `S<int>` and `S<const char *>` are not recorded as
separate symbols, and uses of their members resolve to the primary record's
symbols. A distinct template kind is intentionally not implemented at this
time.

Symbol records include the written and canonical type spellings when Clang can
resolve them. A field whose written type names a typedef, record, or enum also
identifies that type symbol. C++ constructor and destructor methods carry the
`SEMINDEX_PROPERTY_CONSTRUCTOR` or `SEMINDEX_PROPERTY_DESTRUCTOR` property
bits. Attribute priorities are intentionally not recorded.

`semindex_use_t.kind` gives the broad operation (`READ`, `WRITE`,
`ADDR`, or `CALL`). Its `mode` field is the more detailed
`semindex_use_mode_t` bitmask for address, value, and pointer access.

Structured function signatures are available through
`semindex_function_signature_count()` and
`semindex_get_function_signature()`. A signature contains written and
canonical return types, ordered parameters, optional parameter type
identities, and the variadic flag. C++ method signatures are stored under the
qualified symbol name and list only the declared parameters; the implicit
`this` parameter is not part of the signature.

Local symbols are included by default. Disable them before indexing with
`semindex_set_include_local(s, 0)`. The source scope is selected with
`semindex_set_scope()`.

`semindex_set_points_to_analysis()` enables the experimental, in-memory
indirect-call constraint collector. Its counters are returned by
`semindex_get_points_to_stats()`. This facility is intended for trace-based
capacity measurements: it does not add inferred calls to normal records or
persist constraints in SQLite.

## File fingerprints

Call `semindex_build_file_fingerprints()` after a clean or partial index
operation to derive fingerprints for indexed files. Enumerate them with
`semindex_file_fingerprint_count()` and
`semindex_get_file_fingerprint()`. A fingerprint includes the semantic
records attributed to the file and can be used by storage implementations to
avoid replacing unchanged header records.
