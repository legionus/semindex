# Building semindex

semindex requires a C and C++ compiler, CMake, SQLite 3.35 or newer, and the
LLVM and Clang development files from LLVM 21 or newer.  `clang-format` and the
`sqlite3` command-line program are needed by the development checks and tests.
The performance benchmark additionally uses `/usr/bin/time`.

Check the versions selected from `PATH` before configuring the project:

```sh
llvm-config --version
sqlite3 --version
clang-format --version
```

`llvm-config --version` must report 21 or newer.  CMake uses this program to
find the matching LLVM headers and libraries.

## Fedora

On a Fedora release whose default LLVM is version 21 or newer, install:

```sh
sudo dnf install \
    gcc gcc-c++ cmake \
    llvm-devel clang clang-devel clang-tools-extra \
    sqlite sqlite-devel time
```

`clang-tools-extra` provides `clang-format` on Fedora.  If the default LLVM is
older, supported Fedora releases also provide versioned compatibility packages
such as `llvm21-devel`, `clang21-devel`, and `clang21-tools-extra`.  Install the
three packages from the same LLVM version and verify that the corresponding
`llvm-config` is selected before running CMake.

Package names and available versions can be checked in
[Fedora Packages](https://packages.fedoraproject.org/).

## Ubuntu

On an Ubuntu release whose default `llvm-dev` package is version 21 or newer,
install:

```sh
sudo apt update
sudo apt install \
    build-essential cmake \
    llvm-dev clang libclang-dev libclang-cpp-dev clang-format \
    sqlite3 libsqlite3-dev time
```

After installation, check `llvm-config --version`.  On Ubuntu releases whose
archive contains an older LLVM, use the LLVM project's apt repository.  The
following example installs LLVM 21:

```sh
sudo apt update
sudo apt install wget ca-certificates gnupg
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 21
sudo apt install \
    llvm-21-dev libclang-21-dev libclang-cpp21-dev clang-format-21 \
    build-essential cmake sqlite3 libsqlite3-dev time
export PATH=/usr/lib/llvm-21/bin:$PATH
```

Set `PATH` in every shell used to configure or build semindex.  This ensures
that CMake runs `llvm-config` and `clang-format` from LLVM 21 instead of an
older distribution version.

See [apt.llvm.org](https://apt.llvm.org/) for the repository setup script and
the currently supported Ubuntu releases and LLVM versions.

## Configure and build

From the repository root, run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

The executable is created as `build/semindex` and can be run directly from the
build tree:

```sh
./build/semindex --help
```

CMake also writes `build/compile_commands.json`, which can be used to index the
project itself.

## Tests and formatting

Run the test suite with:

```sh
ctest --test-dir build --output-on-failure
```

Check source formatting with:

```sh
cmake --build build --target format-check
```

Use the `format` target to update files in place:

```sh
cmake --build build --target format
```

See [contribution.md](contribution.md) for the complete patch submission
checklist.

## Troubleshooting

If CMake cannot find `llvm-config`, add the selected LLVM installation's `bin`
directory to `PATH` and configure again.  Remove `build/CMakeCache.txt` or use a
new build directory after changing LLVM versions so that paths from different
LLVM installations are not mixed.

If linking fails with a missing `clang-cpp` library, make sure the LLVM and
Clang development packages come from the same version.  On Ubuntu this means
matching packages such as `llvm-21-dev`, `libclang-21-dev`, and
`libclang-cpp21-dev`.
