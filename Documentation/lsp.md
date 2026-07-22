# Language server

`semindex-lsp` implements the Language Server Protocol over standard input and
standard output. It is a separate executable so protocol messages cannot be
corrupted by normal `semindex` command output.

The server handles JSON-RPC framing, parse and method errors, and the
`initialize`, `initialized`, `shutdown`, and `exit` lifecycle. It reports
UTF-16 as its position encoding and supports `textDocument/definition` and
`textDocument/references` against the stored index. Call hierarchy clients can
use `textDocument/prepareCallHierarchy`, `callHierarchy/incomingCalls`, and
`callHierarchy/outgoingCalls`.

Start the server directly from an editor or an LSP client:

```sh
build/semindex-lsp --database=.semindex/semindex.db
```

By default the server queries all variants in the database. Use
`--variant=NAME` when an editor session should use only one configuration.
Relative paths stored in the index are resolved against the workspace
`rootUri` supplied by the client.

The server reads `Content-Length` framed JSON-RPC messages from standard input
and writes responses to standard output. Diagnostics are written only to
standard error.

Use `--logfile=FILE` to append protocol traffic to a file without corrupting
standard output:

```sh
build/semindex-lsp --logfile=/tmp/semindex-lsp.log
```

Each request or notification is preceded by `CLIENT --> SERVER`, and each
response is preceded by `SERVER --> CLIENT`. The raw JSON payload follows the
marker. The log is flushed after every message so it remains useful when the
server exits unexpectedly. Protocol logs can contain source text and other
client data and should therefore be treated as potentially sensitive.

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

The compiler command database defaults to `commands.db` beside the symbol
database. Select another path with `--commands-database=PATH`. When no variant
is selected, saves update the `general` variant while navigation continues to
query all variants. Use `--variant=NAME` to update and query another variant.
Use `--include-local` when the existing index was created with local symbols;
otherwise save updates omit them.
