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

The default read-only tool set is:

* `search_symbols`;
* `symbol_at`;
* `find_definitions`;
* `find_references`;
* `find_declared_types`;
* `find_callers`;
* `find_callees`;
* `read_source_context`;
* `list_variants`;
* `index_status`.

`index_status` also reports whether the selected file and variant have a saved
compiler command in the command database.

`find_declared_types` accepts the identity returned by `symbol_at` and returns
the declared and canonical C types together with the source file that
contributed them. The canonical type expands typedef chains. Results are
bounded and cursor-paginated.

Record results contain the variant, path, one-based byte position, qualified
symbol, symbol kind, record type, action, access mode, enclosing context, local
status, and stable identities when present. List tools use bounded result
counts and return an opaque `nextCursor` when another page is available.

`find_callers` and `find_callees` return direct relationships by default. Set
`depth` to a value from 2 through 16 to traverse recursively and `nodeLimit` to
bound the number of unique functions visited. Recursive results include the
one-based edge depth and report whether the record or node limit truncated the
graph and whether a cycle was encountered. Recursive traversal does not accept
a pagination cursor; use the result and node limits to bound one complete
traversal. Function identities include the variant, so static functions and
same-named functions from different variants remain distinct. Indirect calls
are not expanded.

Source access is restricted to paths below the canonical workspace root.
`read_source_context` returns at most 200 complete lines and 64 KiB. It uses the
shared source resolver, so current working-tree contents are returned only when
they match indexed metadata; otherwise the recorded Git commit may supply the
indexed contents. Source text is never stored in SQLite.

Tool calls have a 30-second server-side deadline. The server accepts
`notifications/cancelled` while a query is running. Responses are limited to
1 MiB in addition to each tool's record and source limits.

The server opens the symbol database read-only for ordinary tool calls. It does
not expose arbitrary file access, compiler execution, or full-project
indexing.

## Controlled updates

Start the server with `--allow-reindex` to add the `reindex_file` tool:

```sh
semindex mcp --allow-reindex \
	--database=.semindex/semindex.db \
	--commands-database=.semindex/commands.db \
	--workspace="$PWD"
```

The tool accepts one workspace path and an optional variant. It uses only the
compiler command already saved for that exact file and variant; clients cannot
supply compiler arguments. A missing command is returned as a failed indexing
status. `--no-include-local` omits local symbols from updates.

Successful requests return `clean` or `partial`, together with at most 100
diagnostics. Partial frontend results are stored so the index continues to
describe the saved file. Failed frontend or database operations leave the
previous index intact where the indexing pipeline cannot produce usable
records.

Updates are serialized because compiler commands may contain paths relative to
their recorded working directory. Read-only tool calls remain independently
bounded by the normal request deadline and result limits. Cancellation is
observed before and after a single-file update; the Clang frontend itself does
not currently provide an interruption point.
