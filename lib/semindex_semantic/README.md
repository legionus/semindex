# Semantic queries

This library provides protocol-neutral, higher-level semantic operations built
on the public database reader and source resolver.

It owns stable C++ symbol identities and their borrowed C API views so protocol
implementations do not duplicate string lifetime handling.

Keep reusable navigation and semantic relationships here. Wire formats belong
to LSP or MCP, while raw SQL and storage rules belong to the database libraries.
