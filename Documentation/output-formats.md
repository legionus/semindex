# Output formats

The `semindex` command can print indexed records in more than one textual
format.  Select the format with `--format=FORMAT`.

```sh
semindex index --format=dissect --compile-commands build/compile_commands.json file.c
semindex index --format=json --compile-commands build/compile_commands.json file.c
semindex compiler --format=json -- -Iinclude -c file.c -o file.o
```

If `--format` is omitted, `dissect` is used. `semindex compiler` remains quiet
unless `--format` is specified. In both indexing commands, explicitly selecting
a format prints the in-memory index without creating or updating symbol and
compiler-command databases.

## dissect

The `dissect` format is modeled after Sparse `test-dissect` output.  It is
intended to be dense and suitable for comparing semantic indexing results.

Records are grouped by file:

```text

FILE: path/to/file.c

line:col context action local kind name [type]
line:col context mode   local kind name [type]
```

Example:

```text

FILE: tests/test6.c

   2:8                    def   s :var
   4:21                   def   m :var.x                           int
   6:3                    def   v var                              struct :var
   6:3                    -w-   v var                              struct :var
```

Definition and declaration records use:

```text
def
decl
```

Use records use a three-character mode string:

```text
address value pointer
```

Each character describes how the symbol is accessed in that category:

* `-`: not accessed;
* `r`: read;
* `w`: written;
* `m`: both read and written.

For example:

* `-r-`: value read;
* `-w-`: value write;
* `--r`: pointer read;
* `rw-`: address read and value write.

The local marker column is:

* `.` for local symbols or references;
* a space for non-local records.

The kind column uses one-character tags:

```text
v variable
m struct/union member
s struct or union
E enum
e enum constant
t typedef
f function
c class
d macro definition/reference
i include file
L lambda
```

For fields and C++ methods, the printed name is `owner.name`, for example
`S.x` or `Circle.process`. For other records, the printed name is the symbol
name.

Consecutive duplicate use records in the same file are suppressed when their
printed dissect identity is the same.  This mirrors the compact style expected
from dissect-like output.

## JSON

The `json` format is the machine-readable representation of one indexing
result. It is a single JSON document written incrementally without creating a
second buffered copy of the index:

```json
{
  "version": 1,
  "symbols": [
    {
      "kind": "field",
      "name": "x",
      "owner": "S",
      "type": "int",
      "usr": "c:@S@S@FI@x",
      "usr_id": null,
      "context": null,
      "file": "file.c",
      "line": 2,
      "column": 6,
      "local": false,
      "definition": true
    }
  ],
  "uses": [
    {
      "kind": "write",
      "symbol_kind": "field",
      "mode": "-w-",
      "mode_bits": 8,
      "name": "x",
      "owner": "S",
      "type": "int",
      "usr": "c:@S@S@FI@x",
      "usr_id": null,
      "context": "update",
      "context_usr": null,
      "context_usr_id": null,
      "file": "file.c",
      "line": 8,
      "column": 4,
      "local": false
    }
  ]
}
```

`symbols` contains declarations and definitions. `uses` keeps the explicit
`read`, `write`, `address`, or `call` classification as well as the dissect
mode string and its numeric bitmask. Optional strings and unavailable stable
IDs are `null`. Function and call identities are hexadecimal strings rather
than JSON numbers so all 64 bits remain exact.

Function symbols with entry-point attributes include a `properties` array.
The currently emitted values are `constructor` and `destructor`; symbols
without properties omit the field. Structured function signatures and
canonical type relationships are parser and database APIs and are not part of
JSON output version 1.

Consumers must check `version` before interpreting the document. New optional
fields may be added without changing the version; incompatible structural or
semantic changes require a new version.

## Search output

`semindex search` uses the same configurable output style as `semind search`.
Its default format is:

```text
(%{mode}) %{variant_file}\t%{line}\t%{column}\t%{context}\t%{source}
```

Select another format with `-f STRING` or `--format=STRING`.  Each result gets
one output line.  The following substitutions are available:

* `%{file}`: source file path;
* `%{variant}`: index variant;
* `%{variant_file}`: variant-qualified source file path in `variant:path` form;
* `%{line}`: line number;
* `%{column}`: column number;
* `%{context}`: containing function context;
* `%{symbol}`: qualified symbol name;
* `%{record}`: `definition`, `declaration`, or `reference`;
* `%{mode}`: access mode;
* `%{kind}`: full symbol kind name;
* `%{kind_char}`: one-character symbol kind;
* `%{source}`: source line text.

Use `%%` to insert a literal percent sign. The escapes `\t`, `\r`, and `\n`
insert a tab, carriage return, and newline. Other escaped characters are
inserted literally. Unknown or unterminated fields are errors, including when
the search returns no records.

Definitions use the mode `def`, declarations use `decl`, and uses have the
same three-character access mode described for the `dissect` format. The
`%{kind_char}` field uses the same one-character tags as `dissect`.

For example:

```sh
semindex search --format='%{mode} %{kind} %{symbol} at %{file}:%{line}:%{column}' \
  'task_struct.pid'
```

The default format uses `%{variant_file}` so results from different variants
remain distinguishable. Use `%{file}` in a custom format when only the
physical source path is wanted.

Indexing commands store records in the `general` variant unless
`--variant=NAME` is specified.  Search covers every variant by default and can
be restricted with an exact name or GLOB pattern:

```sh
semindex search --variant='*-defconfig' task_struct.pid
```

Search results can be filtered by access mode with `-m MODE` or
`--mode=MODE`.  `MODE` accepts `def`, a one-character shorthand, or a
three-character access mode:

* `def`: definitions;
* `r`: any read access;
* `w`: address or value write access;
* `m`: any read or write access;
* `-`: records without access bits;
* `rwm` combinations such as `-w-` or `--r`: access in a specific position.

As in `semind`, a result matches when any requested access bit is present.
Mode filters other than `def` apply only to use records; `def` applies only to
symbol definitions.

## Stability

The dissect and search text formats are project interfaces for tests and
development, not machine-readable APIs. The JSON structure is versioned, but
source locations, type spelling, and USR strings still come from Clang and may
change with the indexed source, compiler version, or compile flags.

In-process tooling can use the C API in `include/semindex.h` instead of parsing
command output.

Function definitions marked with `.type` or `.ent` in preprocessed assembly
sources have symbol kind `function` and an empty type. They do not have a Clang
USR because no C or C++ declaration exists in the assembly translation unit.
Name and symbol kind allow queries and language-server definition lookup to
associate them with matching C declarations.

Direct calls within marked assembly functions are emitted as `call` uses with
the containing function name as their context. The indexer recognizes common
direct-call instructions after C preprocessing and omits register-indirect
forms. Assembly calls have no Clang USR or context USR, so they are available
to symbol and mode searches but not to identity-based call-graph queries.
