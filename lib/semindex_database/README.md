# Public database reader

This shared library implements the public read-only API declared in
`include/semindex_database.h`.

Keep ABI-facing database handles, streaming record queries, and public reader
semantics here. Internal CLI formatting, database writes, and compiler command
storage belong elsewhere.
