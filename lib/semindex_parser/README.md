# Clang parser

This shared library owns Clang AST, preprocessor, and assembly integration and
constructs the in-memory semantic index exposed by `include/semindex.h`.

Keep language semantics and source-location recovery here. CLI policy, SQLite
storage, and protocol handling must remain outside this library.
