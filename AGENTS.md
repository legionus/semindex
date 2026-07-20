# AI agent instructions

These instructions apply to the entire repository.

## Before changing code

* Read the relevant documents in `Documentation/`, especially
  `index-database.md`, `command-database.md`, and `performance.md`.
* Inspect the existing implementation and tests before proposing a new
  abstraction or database layout.
* Treat `external/sparse` and `external/linux` as read-only reference trees.
* Preserve unrelated user changes and untracked files.  Do not remove or
  rewrite work that was not created for the current task.
* If a required compiler, profiler, or diagnostic tool is unavailable, stop
  and ask the user to install it.  Do not replace a required measurement with
  an estimate.

## Architecture and implementation

* Keep Clang, AST, and preprocessor integration in the C++ library.  Keep the
  CLI, SQLite storage, and output implementations in C unless there is a clear
  architectural reason to move a boundary.
* Follow the existing Linux-kernel-inspired C and C++ style and run the
  repository's `clang-format` checks.
* Do not add project-specific compatibility headers for source trees being
  indexed.  Compiler argument sanitization must remain generic and narrowly
  justified.
* Preserve the separation between the symbol database and the compiler command
  database.
* Do not add per-record SQL lookups, normalized tables, new indexes, or buffered
  copies of the complete result set without measuring their effect.
* When changing a SQL query, inspect its `EXPLAIN QUERY PLAN` output and ensure
  exact symbol lookup still uses an index.

## Tests and performance

* Add or update `.c`, `.expect`, and `.dissect.expect` fixtures when changing C
  language indexing semantics or source locations.
* Run the full build, tests, format check, and `git diff --check` before
  finishing.
* Treat changes to AST/preprocessor traversal, record construction, SQLite
  schema or queries, transactions, header handling, compiler command capture,
  and output buffering as performance-sensitive.
* For a performance-sensitive change, compare the baseline and candidate using
  `scripts/benchmark.sh` and follow `Documentation/performance.md`.
* Database or concurrency changes must also be tested with parallel writers.
  A run that reports `database is locked`, loses records, or silently skips a
  translation unit is a failure.
* Do not accept a median regression greater than 5 percent in indexing time,
  peak memory, database size, or exact-search latency without explaining the
  tradeoff and obtaining user approval.

## Repository hygiene

* Do not commit generated build directories, `.semindex/`, SQLite databases,
  profiler output, or editor temporary files.  In particular, leave `host/`
  untracked.
* Keep patches focused.  Do not combine functional changes, schema redesigns,
  and unrelated refactoring.
* Do not create or rewrite commits unless the user requested it.  When making
  a commit, follow `Documentation/contribution.md` and include the DCO
  `Signed-off-by:` trailer.
* The text of the message must be exclusively in english.
* In the commit message always focus on why the changes are being made, not what
  the code changes do. Use the body to explain motivation, observable impact,
  and any important constraints.
* Always make the commit message in kernel style.
