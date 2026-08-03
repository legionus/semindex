# Indirect call analysis

semindex records an indirect call such as `fn()` or `ops->read()` as a `CALL`
use of the function-pointer variable or field. It does not currently turn that
use into caller-to-callee edges because the AST does not identify one direct
callee.

Inferring every function with a compatible type would produce a large and
misleading graph. Indirect-call candidates must instead come from explicit
points-to constraints observed in indexed source.

## Initial model

The first implementation should be flow-insensitive and context-insensitive.
Its nodes are stable identities for function-pointer variables, parameters,
and fields. It needs only two constraint kinds:

* `pointer -> function` for an address stored in a pointer;
* `pointer -> pointer` for a copied function pointer.

The transitive closure of these constraints gives candidate functions for an
indirect call through the same pointer identity. Keeping constraints rather
than expanded call edges avoids repeating the same candidate set at every
callsite and allows query-time bounds.

The first extraction stage should recognize:

* variable initializers and assignments such as `fn = target`;
* function designators with or without an explicit address operator;
* copies such as `dst = src`;
* designated and positional initializers for function-pointer fields;
* assignments to function-pointer fields.

Casts and parentheses may be stripped when Clang preserves the underlying
function or pointer identity. Conditional expressions may contribute both
arms. Null pointer constants do not create a target.

## Deferred propagation

Interprocedural propagation is deliberately separate. Mapping an argument to a
function-pointer parameter, propagating returned function pointers, and
tracking pointers stored through aliases require call-context or memory-alias
constraints. Adding them to the initial model would make both precision and
storage cost difficult to evaluate.

The initial result is conservative only for the supported assignment forms. A
query must report that its candidates are incomplete; it must not present the
result as a sound whole-program callgraph.

## Storage boundary

No schema should be added before measuring extracted constraints. A trace-only
prototype should count, per translation unit:

* indirect callsites;
* direct target constraints;
* pointer-copy constraints;
* constraints rejected because either side lacks a stable identity;
* distinct pointer and function identities;
* duplicate constraints removed before database staging.

Measurements must include the repository benchmark and a parallel Linux kernel
indexing run. Report aggregate and per-translation-unit counts together with
the estimated rows and bytes for both raw constraints and expanded call edges.

The database design may proceed only if it:

* stores constraints once rather than candidates per callsite;
* preserves variant and stable symbol identities;
* replaces constraints with the same file lifecycle as semantic records;
* supports bounded traversal without scanning unrelated constraints;
* does not add per-record SQL lookups to indexing;
* stays within the normal five-percent performance and size limits, or receives
  explicit approval for a measured regression.

## Query semantics

Direct callgraph results remain unchanged. A future opt-in candidate query
should return the indirect callsite, pointer identity, candidate function
identity, and the constraint path that justified the candidate. It must expose
truncation and unsupported propagation so LSP and MCP clients can distinguish
proven direct edges from inferred candidates.
