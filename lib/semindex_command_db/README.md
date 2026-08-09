# Compiler command database

This library owns storage, lookup, and export of compiler commands in
`commands.db`, including its schema and path normalization rules.

Keep symbol records and semantic queries out of this library. It may depend on
SQLite and generic support helpers, but not on the symbol index database.
