# Callgraph queries

semindex records direct function calls as caller-to-callee edges.  Each edge
contains the display names and stable IDs of both functions, the index variant,
and the source location of the call expression.

Show functions called by `worker`:

```sh
semindex callgraph --callees=worker
```

Show functions that call `schedule`:

```sh
semindex callgraph --callers=schedule
```

The default output contains one line per callsite:

```text
worker -> helper	general:path/to/file.c:20:2
```

Recursive calls are included.  Multiple calls between the same two functions
remain separate because their source locations differ.

## Function identity

Names are convenient for interactive queries but are not globally unique for
functions with internal linkage.  Use `--show-id` to include stable identities
in tab-separated output:

```sh
semindex callgraph --callees=worker --show-id
```

The fields are:

```text
CALLER  CALLER_ID  CALLEE  CALLEE_ID  VARIANT:FILE:LINE:COLUMN
```

Pass a displayed hexadecimal identity back with `--id=ID` to select one
function when several static functions have the same name:

```sh
semindex callgraph --callees=worker --id=62e8d0271dbb4479
```

The name remains required with `--id`. The ID is a deterministic 64-bit hash
of Clang's function USR and disambiguates functions that have the same display
name without storing the full USR at every call site.

## Filters

Use `--variant=PATTERN` to select an indexed build configuration and
`--path=PATTERN` to select callsite files.  Patterns containing `*`, `?`, or
`[]` use SQLite GLOB syntax; other values match exactly.

```sh
semindex callgraph --callers=schedule --variant=x86-defconfig
semindex callgraph --callees=worker --path='kernel/*'
```

Without `--variant`, edges from all indexed variants are returned.  The
variant-qualified callsite in each output row keeps otherwise identical edges
distinguishable.

## Scope and limitations

The current callgraph contains only direct calls for which Clang's
`CallExpr::getDirectCallee()` resolves a `FunctionDecl`.  Calls introduced by a
macro are included after preprocessing when they resolve directly.

Indirect calls such as `fn()` or `ops->read()` are not resolved to possible
function targets and are not returned by `semindex callgraph`.  semindex does
not infer edges from compatible function types because doing so would create a
large number of false relationships.  Capturing unresolved indirect callsites
and later adding points-to analysis are separate future steps.

As with the symbol index, only code active under the compiler arguments for an
indexed variant appears in the graph.  Index multiple variants to combine
relationships from different preprocessor configurations.
