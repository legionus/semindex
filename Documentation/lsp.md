# Language server

`semindex-lsp` implements the Language Server Protocol over standard input and
standard output. It is a separate executable so protocol messages cannot be
corrupted by normal `semindex` command output.

The current protocol foundation handles JSON-RPC framing, parse and method
errors, and the `initialize`, `initialized`, `shutdown`, and `exit` lifecycle.
It reports UTF-16 as its position encoding. Symbol navigation capabilities are
not advertised until their request handlers are available.

Start the server directly from an editor or an LSP client:

```sh
build/semindex-lsp
```

The server reads `Content-Length` framed JSON-RPC messages from standard input
and writes responses to standard output. Diagnostics are written only to
standard error.
