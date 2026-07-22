# Contributing to semindex

semindex follows a Linux-kernel-style patch workflow.  Keep changes focused,
explain the reason for the change in the commit message, and make sure every
patch can be reviewed on its own.

## Developer Certificate of Origin

Contributions must follow the Developer Certificate of Origin process used by
the Linux kernel.  Add a `Signed-off-by:` line to every commit to certify that
you have the right to submit the change under the project's license.

Use `git commit -s` or add the trailer manually:

```text
Signed-off-by: Your Name <you@example.com>
```

Do not add someone else's sign-off without their explicit agreement.

## Formatting

Source code is formatted with `clang-format` using the repository
`.clang-format` file.  Run the formatter before submitting patches:

```sh
cmake --build build --target format
```

Check that the tree is already formatted:

```sh
cmake --build build --target format-check
```

## Tests

Run the full test suite before submitting changes:

```sh
ctest --test-dir build --output-on-failure
```

Also make sure the project still builds:

```sh
cmake --build build
```

For changes that affect indexed source locations, update the corresponding
`tests/*.expect` and `tests/*.dissect.expect` files in the same patch.

## Performance

Changes to AST traversal, record construction, SQLite storage or queries,
header handling, compiler command capture, and output buffering require an A/B
performance comparison.  Follow [performance.md](performance.md) and run the
quick benchmark against the baseline and candidate builds:

```sh
scripts/benchmark.py \
    --baseline=/path/to/baseline/build/semindex \
    --candidate=build/semindex
```

Database and concurrency changes also require a parallel indexing test.  Do
not accept locked database errors, lost records, or silently skipped files.

## Patch checklist

Before sending a patch, verify:

* the commit carries a valid `Signed-off-by:` trailer;
* `cmake --build build` succeeds;
* `cmake --build build --target format-check` succeeds;
* `ctest --test-dir build --output-on-failure` succeeds;
* generated expectation changes are intentional and reviewed;
* performance-sensitive changes include baseline and candidate measurements.
