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

Start the server through the dispatcher or invoke its helper directly:

```sh
build/semindex lsp --database=.semindex/semindex.db
build/semindex-lsp --database=.semindex/semindex.db
```

By default the server queries all variants in the database. Use
`--variant=NAME` when an editor session should use only one configuration.
Relative paths stored in the index are resolved against the workspace
`rootUri` supplied by the client. If `rootUri` is absent or null, the first
valid URI in `workspaceFolders` is used instead. The workspace root must match
the Git repository root used when the index was created.

If the symbol database does not exist, the server creates it with an empty
index instead of refusing to start. Opening and saving files begins populating
that database. The server prefers a command already stored in the command
database, then the compilation database selected with
`--compile-commands=PATH`, then `compile_commands.json` in the repository root.
If none is available, it indexes the file with a minimal command. The fallback
command searches the repository root and its `include` directory when present;
it is approximate and is not stored in the command database.

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

The server indexes a file on `textDocument/didOpen` and
`textDocument/didSave`. It reads the saved file from disk; unsaved buffer text
and incremental `textDocument/didChange` updates are not indexed.

When a saved command or compilation-database command is available, clean and
partial results replace that source's records in the symbol database before
the next request is processed. This matches `semindex compiler` and
`semindex index`: a partial index describes the current saved source instead of
retaining stale clean records. The selected compilation-database command is
also saved in `commands.db`. A failed frontend run with no usable records
leaves the existing persistent index unchanged.

When only the approximate fallback command is available, its clean or partial
main-file records are kept in a transient overlay rather than persisted.
Definition, reference, and document-highlight requests consult that overlay
and the persistent database. Opening a header without a compiler command can
therefore provide navigation within that document without treating the
approximate command as project configuration.

Frontend diagnostics are published through
`textDocument/publishDiagnostics`. Call hierarchy always uses persistent
records; a fallback overlay is not added to the stored call graph.

The compiler command database defaults to `commands.db` beside the symbol
database. Select another path with `--commands-database=PATH`. When no variant
is selected, saves update the `general` variant while navigation continues to
query all variants. Use `--variant=NAME` to update and query another variant.
Save updates preserve local symbols by default. Use `--no-include-local` when
the existing index intentionally omits them.

## Editor integration

See [lsp-editors.md](lsp-editors.md) for editor setup and protocol-log
diagnostics. The editor must use the same project root as the index so
repository-relative paths resolve consistently.
