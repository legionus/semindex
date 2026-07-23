# Output formats

The `semindex` command can print indexed records in more than one textual
format.  Select the format with `--format=FORMAT`.

```sh
semindex index --format=dissect --compile-commands build/compile_commands.json file.c
semindex index --format=json --compile-commands build/compile_commands.json file.c
semindex compiler --format=json -- -Iinclude -c file.c -o file.o
```

If `--format` is omitted, `dissect` is used. `semindex compiler` remains quiet
unless `--format` is specified.

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
v variable or enum constant
m struct/union member
s struct or union
e enum
t typedef
f function
d macro definition/reference
i include file
```

For fields, the printed name is `owner.name`, for example `S.x`.  For other
records, the printed name is the symbol name.

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

Consumers must check `version` before interpreting the document. New optional
fields may be added without changing the version; incompatible structural or
semantic changes require a new version.

## Search output

`semindex search` uses the same configurable output style as `semind search`.
Its default format is:

```text
(%m) %F\t%l\t%c\t%C\t%s
```

Select another format with `-f STRING` or `--format=STRING`.  Each result gets
one output line.  The following substitutions are available:

* `%f`: source file path;
* `%F`: variant-qualified source file path in `variant:path` form;
* `%v`: index variant;
* `%l`: line number;
* `%c`: column number;
* `%C`: containing function context;
* `%n`: qualified symbol name;
* `%m`: access mode;
* `%k`: one-character symbol kind;
* `%s`: source line text.

The escapes `\t`, `\r`, and `\n` insert a tab, carriage return, and newline.
Other escaped characters are inserted literally.

Definitions use the mode `def`, declarations use `decl`, and uses have the
same three-character access mode described for the `dissect` format.  The `%k`
substitution uses the same one-character tags as `dissect`.

For example:

```sh
semindex search --format='%m %k %n at %f:%l:%c' 'task_struct.pid'
```

The default format uses `%F` so results from different variants remain
distinguishable.  Use `%f` in a custom format when only the physical source
path is wanted.

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
