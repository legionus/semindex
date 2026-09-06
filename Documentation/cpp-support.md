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
  ordinary structure fields; for namespaced members the C++ `::` chain is kept
  only inside the namespace name and the `.` separator between the class and
  its member is preserved (for example `alpha::Point.process`, and a nested
  form such as `alpha::beta::Node.value`).
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
* Explicit `new T`, `new T[N]`, `new (addr) T`, `delete p`, and `delete[] p`
   are recorded as a `READ` reference on the constructed/deleted type record
   `T`, the same way a `static_cast` is recorded on its target. The
   allocation/deallocation functions (`operator new` / `operator delete`)
   are not recorded. `new` / `delete` on primitives produce no reference.
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
    * `test_cpp_template` (primary template indexed as a dedicated
      `SEMINDEX_SYMBOL_TEMPLATE` symbol (dissect kind `T`), with members as
      the usual methods and fields; specializations and explicit
      instantiations are not recorded as separate symbols, and uses of a
      specialization's members resolve to the primary's own members);
   * `test_cpp_lambda` and `test_cpp_lambda1` (lambda kind `L`
     `SEMINDEX_SYMBOL_LAMBDA`, direct call recorded);
   * `test_cpp_base` (multiple inheritance: base class recorded as `READ`);
   * `test_cpp_casts` (explicit user-written casts recorded as `READ` on the
     destination type).
   * `test_cpp_new_delete` (explicit `new T` / `new T[N]` / placement
     `new` / `delete p` / `delete[] p` recorded as `READ` on the
     constructed/deleted type record).

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

## Unsupported C++ constructs (details)

The table below lists the C++-specific constructs that are not indexed (or are
indexable only in a limited form) by the parser today. For the shared C and C++
gaps — LSP completion/rename, persistent type graph, points-to analysis, and
preprocessed-only code — see the [Current
limitations](../README.md#current-limitations) section of the README. Rows that
show `—` in the code location column do not have a single obvious visitor to
point at.

| C++ construct | Clang AST node | Current behavior |
|---|---|---|
| Function templates as dedicated symbols | `FunctionTemplateDecl` | Indexed as `SEMINDEX_SYMBOL_FUNCTION`; template nature only visible in the USR (`index_ast.cpp:199-211`) |
| Variable / alias templates | `VarTemplateDecl`, `AliasTemplateDecl` | No dedicated symbol kind |
| Explicit specializations `S<int>` / partial specializations | `ClassTemplateSpecializationDecl`, `TSK_*` | Declaration is skipped (`VisitRecordDecl` :809-811, `VisitFieldDecl` :616-618, `VisitFunctionDecl` :883-888); uses resolve to the primary via `primaryValueDecl` (`index_ast.cpp:1158-1210`) |
| Explicit instantiation `template class S<int>;` | `TemplateSpecializationType` in an instantiation context | Not written to the DB |
| Virtual-dispatch calls | `CXXMemberCallExpr` whose vtable `getDirectCallee() == nullptr` | Early-return; counted only in the points-to stats when `semindex_set_points_to_analysis()` is enabled (`index_ast.cpp:947-964`) |
| Indirect calls through a function pointer | `CallExpr` with a function-pointer callee | `if (!FD) return true` — not written outside the experimental points-to path (`index_ast.cpp:947`) |
| Member-function-pointer invocation | `CXXBindTIExpr` / `MemberCallExpr` on a pointer base | `pointerDecl()` does not recognise it (`index_ast.cpp:1054-1071`) |
| Placement `new` | `CXXNewExpr` + `OperatorNewExpr` | The `READ` on the record is recorded; the `operator new` call is not (`index_ast.cpp:984-1004`) |
| `new T[]` / `new (loc) T[]` as distinct semantics | `FunctionDecl` with `OO_New` / `OO_Delete` | Indexed as a plain `function`; the array / placement semantics are not modelled as a separate kind |
| RTTI: `typeid x` | `CXXTypeidExpr` | Not handled |
| RTTI: `dynamic_cast` as dispatch | `CXXDynamicCastExpr` / `ImplicitCastExpr` | Recorded only as a `READ` on the destination type; the virtual-dispatch meaning is not visible (`index_ast.cpp:671-702`) |
| Implicit `static_cast` / `const_cast` / `explicit` on the source type | `CXXFunctionalCastExpr`, `CXXReinterpretCastExpr`, `CXXBindTemporaryExpr` | Only the destination is recorded; source / intermediate temporaries are not (rule: "user-written only") |
| `try` / `catch` | `CXXTryStmt`, `CXXCatchStmt` | Not handled |
| `throw` | `CXXThrowExpr` | Not recorded |
| Exception specifiers / `noexcept` | Function / `ParmVarDecl` attributes | Not recorded in symbol properties (`index_ast.cpp:1007-1026`) |
| Nested / anonymous classes inside a function | `CXXRecordDecl` with an empty name | Skipped as separate symbols; lambda closure is the exception (`index_ast.cpp:806-808, 1557-1583`) |
| `using` alias / qualified `using` declaration | `TypedefDecl` / `UsingShadowDecl` | `TypedefDecl` is indexed as a typedef; `UsingShadowDecl` and qualified aliases are not handled (`index_ast.cpp:639-669`) |
| Friend functions / `friend` declarations | `FriendDecl` (not a `FunctionDecl`) | Not recorded |
| `goto` / label references | `LabelStmt` / `LabelDecl` | Not recorded (shared C gap) |
| `constexpr` / `consteval` / CTAD guides | `DeductionGuideDecl`, `FunctionDecl` | Not handled |
| `static_assert` | `StaticAssertDecl` | Not a dedicated symbol |
| LSP completion / rename / unsaved buffers for C++ | `—` | Explicitly not supported (shared LSP gap, see [README — Current limitations](../README.md#current-limitations)) |
| Points-to analysis (regular query path) | any function-pointer / member-pointer use | Available only behind the experimental `semindex_set_points_to_analysis()` (`index_ast.cpp:460-470, 557-609, 947-964`) |
| Persistent type graph | any `TypeDecl` | Only stored declared-to-canonical relations (see [README — Current limitations](../README.md#current-limitations)) |
