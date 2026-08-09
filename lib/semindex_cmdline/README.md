# Command-line support

This library contains argument parsing, indexing options, and output adapters
shared by the `semindex-*` command-line tools.

Keep command-independent CLI policy here. Individual subcommand behavior and
executable entry points belong under `tools/`; database and parser logic belong
to their respective libraries.
