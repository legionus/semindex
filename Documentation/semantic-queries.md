# Semantic queries

`semindex_semantic` provides protocol-neutral lookup of semantic records at a
source position. LSP and MCP code use shared semantic identities and query
helpers instead of reimplementing string ownership, database path selection,
and record disambiguation.

The service accepts a physical source path and one-based byte line and column
coordinates. Protocol adapters remain responsible for URI decoding and for
converting protocol-specific coordinates such as LSP UTF-16 positions.

Position lookup tries the path forms produced by `SemindexSourceResolver`, in
order. This permits one database to contain repository-relative paths while a
client identifies a file by its absolute path. A variant may be selected
explicitly; an empty variant searches all indexed variants.

`SemindexQueryRecord` owns its strings because strings received through the C
database callback are valid only for the duration of that callback. Duplicate
path candidates are collapsed by stable symbol identity when it is available.
Records without a stable identity use their variant, path, symbol, context,
kind, and local status as the fallback identity.

An optional `SemindexQueryOverlay` lets an interactive frontend replace
persistent records for a file. LSP uses it for records produced by the
approximate fallback command when no saved or compilation-database command is
available. Configured clean and partial results are persisted instead. The
common service does not know how an overlay is produced and contains no
protocol JSON.

The service is read-only. It does not add tables, indexes, or records to the
symbol database.
