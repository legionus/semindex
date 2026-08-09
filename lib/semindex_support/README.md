# Runtime support

This library contains small process-wide utilities shared across otherwise
independent modules: filesystem creation, repository path handling, and
performance tracing.

Keep only generic infrastructure with multiple consumers here. Domain logic,
database schemas, output formats, and protocol behavior must remain in their
owning libraries.
