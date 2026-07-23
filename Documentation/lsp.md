# Language server

`semindex-lsp` implements the Language Server Protocol over standard input and
standard output. It is a separate executable so protocol messages cannot be
corrupted by normal `semindex` command output.

The server handles JSON-RPC framing, parse and method errors, and the
`initialize`, `initialized`, `shutdown`, and `exit` lifecycle. It reports
UTF-16 as its position encoding and supports `textDocument/definition` and
`textDocument/references` against the stored index. The
`textDocument/documentHighlight` request classifies occurrences in the current
file as text, read, or write highlights. Call hierarchy clients can use
`textDocument/prepareCallHierarchy`, `callHierarchy/incomingCalls`, and
`callHierarchy/outgoingCalls`.

Start the server directly from an editor or an LSP client:

```sh
build/semindex-lsp --database=.semindex/semindex.db
```

By default the server queries all variants in the database. Use
`--variant=NAME` when an editor session should use only one configuration.
Relative paths stored in the index are resolved against the workspace
`rootUri` supplied by the client. If `rootUri` is absent or null, the first
valid URI in `workspaceFolders` is used instead.

The server reads `Content-Length` framed JSON-RPC messages from standard input
and writes responses and notifications to standard output.

Use `--logfile=FILE` to append protocol traffic to a file without corrupting
standard output:

```sh
build/semindex-lsp --logfile=/tmp/semindex-lsp.log
```

Each request or notification is preceded by a UTC timestamp and
`CLIENT --> SERVER`, and each response is preceded by a timestamp and
`SERVER --> CLIENT`. Timestamps have microsecond precision, so request latency
can be measured directly from the log. The raw JSON payload follows the marker.
The log is flushed after every message so it remains useful when the server
exits unexpectedly. Protocol logs can contain source text and other client data
and should therefore be treated as potentially sensitive.

## Call hierarchy

Call hierarchy items carry the index variant and stable function identity in
their opaque `data` field. This keeps same-named static functions in different
files separate. Incoming and outgoing results group all callsite ranges for
each related function.

Only direct calls are represented. Calls through function pointers do not have
a statically resolved callee and are therefore omitted from the hierarchy.

## Index updates

The server advertises save-only text synchronization. On
`textDocument/didSave`, it loads the compiler command previously stored by
`semindex compiler` or `semindex index`, indexes the saved file, and replaces
that file's records in the symbol database before processing the next request.
Unsaved buffer contents are not indexed.

When Clang recovers a partial AST after an error, the server publishes its
diagnostics with `textDocument/publishDiagnostics` and keeps the recovered
main-file records in memory. Definition, reference, and document-highlight
requests use this overlay while records from other files continue to come from
the last clean database. A clean save replaces the database records and clears
the overlay and diagnostics. A failed frontend run discards the overlay and
falls back to the last clean database. Partial records never replace persistent
records.

Call hierarchy requests continue to use the persistent index. Recovery
expressions may preserve a referenced function without preserving enough
semantics to classify the operation as a call.

The compiler command database defaults to `commands.db` beside the symbol
database. Select another path with `--commands-database=PATH`. When no variant
is selected, saves update the `general` variant while navigation continues to
query all variants. Use `--variant=NAME` to update and query another variant.
Save updates preserve local symbols by default. Use `--no-include-local` when
the existing index intentionally omits them.
