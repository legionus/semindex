# Symbol index database

This library owns symbol index writes and the internal search and callgraph
queries over `semindex.db`.

Keep the symbol schema, transactions, query adapters, and search-specific
formatting here. Compiler commands belong to `semindex_command_db`; the public
reader ABI belongs to `semindex_database`.
