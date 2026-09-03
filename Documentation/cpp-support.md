# C++ support

This page is a single, self-contained, user-facing report of the current state
of C++ support in semindex: what is supported, what is not supported yet, and
how to verify the work. It is kept consistent with [README.md](../README.md)
(its "Current status" and "Current limitations" sections) and with the
[parser API](api.md), [output formats](output-formats.md), and
[building](building.md) documentation.

semindex is, today, an early prototype. C++ support is delivered through the
same parser, index, and query layers used for C; the C++-specific behavior is
documented below and reflected in the committed test fixtures under
[`tests/test_cpp_*.cpp`](../tests/).

## What is supported

Language detection and parsing.

* C++ translation units are parsed with the Clang LibTooling frontend. The
  Clang tool selects the language from the source extension
  (`.cpp`, `.cc`, `.cxx`, `.c++`) or from explicit `-x c++` arguments; no
  hard-coded forcing happens in the argument sanitizer.
* C and C++ share the same index, query, and output code paths, so the two
  languages are indexed and searched with the same commands.

Declarations, definitions, and references.

* Classes and structs are indexed as a single `SEMINDEX_SYMBOL_CLASS` symbol
  (dissect kind `c`); their member functions are `function` symbols and their
  data members are `field` symbols.
* Members are owner-qualified. For classes the qualified name uses the
  `Class.member` form (for example `Circle.process`), the same convention as
  ordinary structure fields; for namespaced members the C++ `::` spelling is
  used (for example `alpha::Point.process`, and a namespaced method as
  `alpha::Point::process`).
* Overloads are kept distinct: two methods with the same name in different
  classes produce different USRs and thus different `usr_id` values, so
  `Point.process` and `Circle.process` never resolve to the same record.
* C++ constructor and destructor methods carry the
  `SEMINDEX_PROPERTY_CONSTRUCTOR` / `SEMINDEX_PROPERTY_DESTRUCTOR` property
  bits.
* Namespaces are first class for naming: the parser passes the qualified
  `ns::Class` owner through, and `ns::Class.member` is the stored qualified
  name of a namespaced member.
* Lambdas are indexed as a dedicated kind `SEMINDEX_SYMBOL_LAMBDA` (kind value
  11, name `lambda`, dissect kind `L`). By convention the lambda symbol name is
  `__lambda`; the enclosing function context is recorded in the USR and the
  `local` flag is set when the lambda appears inside a function body. A direct
  call to the lambda is recorded as a `call` use on the lambda symbol, the same
  way an ordinary function call is recorded.

Uses and access classification.

* Every explicit use is classified as `READ`, `WRITE`, `ADDR`, or `CALL`.
* Explicit user-written casts are recorded as a `READ` reference on the
  destination type symbol. This covers the C-style `(T)x`, the C++
  `static_cast<T>(x)`, `dynamic_cast<T*>(x)`, and `const_cast<T>(x)` spellings.
  The recorded target is the destination type (the pointee class for a pointer
  cast), not the source object; implicit casts introduced by conversion or
  member lookup are not recorded.
* A base-class specifier in a class definition (for example the `B` in
  `class D : public B {}`) is recorded as a `READ` use on the base class `B`,
  matching multiple inheritance; it is not recorded as a call.
* Constructor calls (`CXXConstructExpr`) and method calls (`CXXMemberCallExpr`)
  are recorded as `CALL` uses on the invoked method symbol.

Search and navigation.

* Search by qualified symbol name and access mode works for C++ members, for
  example `semindex search Circle.process`.
* The language server reports C++ member symbols and supports
  `textDocument/hover` (reporting the member kind as `method`),
  `textDocument/definition`, `textDocument/references`,
  `textDocument/documentHighlight`, and call hierarchy
  (`prepareCallHierarchy`, `incomingCalls`, `outgoingCalls`) for C++ sources.

These capabilities are reflected in the primary-template documentation in
[api.md](api.md): the primary template of a templated class is indexed once as
a dedicated `SEMINDEX_SYMBOL_TEMPLATE` symbol (name `template`, dissect kind
`T`) with its members as the usual methods and fields; specializations and
explicit instantiations are not recorded as separate symbols. This is the
intended, supported representation of class templates today.

## What is not supported yet

This is a prototype and the following are not available.

* Function templates, variable templates, and alias templates as dedicated
  symbols. Only the primary template of a templated class is indexed as a
  dedicated `SEMINDEX_SYMBOL_TEMPLATE` symbol; function templates are still
  indexed as `SEMINDEX_SYMBOL_FUNCTION`, and specializations and explicit
  instantiations such as `S<int>` are not recorded as separate symbols. Uses
  of a specialization's members resolve to the primary record's symbols.
* Lambdas and anonymous classes beyond the single `SEMINDEX_SYMBOL_LAMBDA`
  kind. Only the lambda declaration and its direct calls are indexed; nested
  anonymous types have no dedicated representation.
* Virtual-dispatch call resolution. Calls that are resolved only through a
  virtual table are not recorded as calls, and function-pointer indirection is
  not resolved to a target.
* RTTI. `dynamic_cast<T*>` is recorded only as an explicit user-written cast to
  the destination type; `typeid` and other run-time type identification are not
  modeled.
* `new` / `delete` expressions are not recorded as references.
* Indirect-call and points-to analysis are not available in the ordinary query
  path (only an experimental, in-memory constraint collector behind
  `semindex_set_points_to_analysis()` in the parser API).
* The language server does not support unsaved buffers, completion, rename, or
  workspace symbols for C++ (or C); it reads saved buffers from disk.
* A persistent type graph beyond the stored declared-to-canonical type
  relationships.

The full list of limitations, including the C-specific ones, is in
[Current limitations in README.md](../README.md#current-limitations) and in the
per-phase "Out of scope" notes in [TODO.md](../TODO.md).

## How to verify

The commands below match the project's build system and the
[building](building.md) documentation. They can be run from the repository root.

1. Configure and build:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
   cmake --build build -j"$(nproc)"
   ```

2. Run the full test suite, which includes the C++ fixtures:

   ```sh
   ctest --test-dir build --output-on-failure
   ```

   Each C++ fixture registers a `json` and a `dissect` output test. The C++
   fixtures covered by the suite are:

   * `test_cpp_method` (classes, owner-qualified fields and methods, overloads,
     constructor calls);
   * `test_cpp_namespace` (namespaced classes and `::`-qualified members);
    * `test_cpp_template` (primary template indexed as a `template` kind;
      member field, member function, explicit specializations, and
      instantiations continue to be recorded);
   * `test_cpp_lambda` and `test_cpp_lambda1` (lambda kind `L`
     `SEMINDEX_SYMBOL_LAMBDA`, direct call recorded);
   * `test_cpp_base` (multiple inheritance: base class recorded as `READ`);
   * `test_cpp_casts` (explicit user-written casts recorded as `READ` on the
     destination type).

   To run only the C++ output tests:

   ```sh
   ctest --test-dir build -R 'test_cpp_' --output-on-failure
   ```

3. Verify the C++ language-server scenario (hover, definition, references, and
   call hierarchy on `Circle.process` from `test_cpp_method.cpp`):

   ```sh
   ctest --test-dir build -R 'semindex-lsp' --output-on-failure
   ```

4. Run the source-format check:

   ```sh
   cmake --build build --target format-check
   ```

5. Confirm the working tree has no accidental whitespace or conflict changes:

   ```sh
   git diff --check
   ```

A build and test pass on a clean tree, together with the format check above, is
how to verify that the supported C++ features are present and that none of the
behavior described in "What is supported" has regressed. Because this is a
documentation-only report, it does not change any of the code, the database
schema, or the fixtures described above.
