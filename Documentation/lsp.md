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
