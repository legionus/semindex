# Language server

`semindex-lsp` implements the Language Server Protocol over standard input and
standard output. It is a separate executable so protocol messages cannot be
corrupted by normal `semindex` command output.

The server handles JSON-RPC framing, parse and method errors, and the
`initialize`, `initialized`, `shutdown`, and `exit` lifecycle. It reports
UTF-16 as its position encoding and supports `textDocument/definition` and
`textDocument/references` against the stored index.

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
