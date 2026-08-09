# Git provenance

This library resolves optional repository provenance and isolates the runtime
backend that uses libgit2.

Keep Git-specific discovery and object handling here. Generic repository path
handling belongs to `semindex_support`, and normal indexing must not acquire an
eager libgit2 dependency.
