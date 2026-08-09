# Incremental index updates

This library resolves compiler commands and coordinates incremental indexing
requested by long-running clients such as LSP and MCP servers.

Keep update policy, cancellation, diagnostics, and transient-index ownership
here. Parsing belongs to `semindex_parser`, while persistence belongs to the
database libraries.
