# Output formats

The `semindex` command can print indexed records in more than one textual
format.  Select the format with `--format=FORMAT`.

```sh
semindex index --format=default --compile-commands build/compile_commands.json file.c
semindex index --format=dissect --compile-commands build/compile_commands.json file.c
semindex compiler --format=dissect -- cc -Iinclude -c file.c -o file.o
```

If `--format` is omitted, `default` is used.

## default

The default format is a simple prototype format intended for tests and manual
inspection.  It is split into two sections:

```text
SYMBOLS:
file:line:column action kind name [type]

USES:
file:line:column use-kind usr
```

Example:

```text
SYMBOLS:
tests/test6.c:2:8 defined  struct   :var
tests/test6.c:4:21 defined  field    x          int
tests/test6.c:6:3 defined  var      var        struct :var

USES:
tests/test6.c:6:3 WRITE c:@var
```

Symbol fields are:

* `file`, `line`, `column`: source location reported by Clang;
* `action`: `defined` or `declared`;
* `kind`: symbol kind;
* `name`: symbol name;
* `type`: textual type, present only when known and non-empty.

Use fields are:

* `file`, `line`, `column`: source location of the reference;
* `use-kind`: `READ`, `WRITE`, `ADDR`, or `CALL`;
* `usr`: Clang USR of the referenced symbol, or a semindex-generated stable
  identifier for entities such as macros and include files.

Current symbol kinds are:

```text
var field struct union enum enumerator typedef function macro file
```

The default format intentionally exposes less information than the internal
record model.  For example, it does not print access mode bits, owner names, or
the current function context for each record.

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

The text formats are currently project interfaces for tests and development,
not a finalized machine-readable API.  Source locations, type spelling, and
USR strings come from Clang and may change when the indexed source, compiler
version, or compile flags change.

For tooling that needs a stable programmatic interface, prefer the C API in
`include/semindex.h` over parsing command output.
