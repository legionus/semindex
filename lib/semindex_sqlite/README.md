# SQLite helpers

This library contains low-level SQLite wrappers shared by database
implementations, such as statement preparation and value binding.

Keep helpers generic and free of symbol or compiler-command schema policy.
Schema definitions and SQL queries belong to the database that owns them.
