# Model Context Protocol server

`semindex-mcp` exposes the semantic index through read-only Model Context
Protocol tools. Start it through the dispatcher or invoke the helper directly:

```sh
semindex mcp --database=.semindex/semindex.db --workspace="$PWD"
semindex-mcp --database=.semindex/semindex.db --workspace="$PWD"
```

The server implements the MCP `2025-11-25` lifecycle and tools capability over
newline-delimited JSON-RPC on standard input and output. It accepts
`--variant=NAME` to restrict every query to one variant and `--logfile=FILE` to
append timestamped protocol traffic. The default workspace is the current
directory.

## Tools

The initial tool set is:

* `search_symbols`;
* `symbol_at`;
* `find_definitions`;
* `find_references`;
* `find_callers`;
* `find_callees`;
* `read_source_context`;
* `list_variants`;
* `index_status`.

Record results contain the variant, path, one-based byte position, qualified
symbol, symbol kind, record type, action, access mode, enclosing context, local
status, and stable identities when present. List tools use bounded result
counts and return an opaque `nextCursor` when another page is available.

Source access is restricted to paths below the canonical workspace root.
`read_source_context` returns at most 200 complete lines and 64 KiB. It uses the
shared source resolver, so current working-tree contents are returned only when
they match indexed metadata; otherwise the recorded Git commit may supply the
indexed contents. Source text is never stored in SQLite.

Tool calls have a 30-second server-side deadline. The server accepts
`notifications/cancelled` while a query is running. Responses are limited to
1 MiB in addition to each tool's record and source limits.

The server opens the symbol database read-only for each tool call. It does not
expose indexing, compiler execution, or arbitrary file access.
