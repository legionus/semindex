# Source resolution

`semindex_source` provides source path and content access shared by protocol
frontends. Protocol-specific URI handling, coordinate conversion, and JSON
remain outside this library.

The resolver accepts a workspace root and resolves relative index paths below
that root. Absolute paths remain absolute. It also produces the path candidates
used to accommodate indexes created with repository-relative or absolute paths.

## Working tree

`readSource()` requires an index database, variant, indexed path, one-based
first line, line count, and byte limit. It first reads the stored modification
time and size through `semindex_db_find_file()` and compares them with the
physical file. The metadata is checked again afterwards so a file changed
during the operation is not returned as current source.

The result status is one of:

* `Current`: the metadata matched and the bounded line range came from the
  working tree;
* `NotIndexed`: the database has no matching `(variant, path)` row;
* `Missing`: an indexed working-tree file no longer exists;
* `Drifted`: the current file metadata differs from the index.

Missing and changed indexed files are marked as drifted and do not return their
working-tree contents. When the variant has a repository root and Git commit,
the resolver instead reads the indexed path from that commit through libgit2.
The original `Missing` or `Drifted` status and `drifted` flag are preserved,
while `origin` is set to `GitCommit`. Without provenance, libgit2, or a matching
blob, the result contains no source lines.

Both working-tree and Git reads return only the requested line range. A byte
limit smaller than the next complete line stops the result before that line and
sets `byte_limit_hit`. Git blob contents are read directly from libgit2 and are
not copied into the index database.

Modification time and size are a fast drift check, not a content identity.
Source text is never stored in SQLite.
