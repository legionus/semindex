# LSP editor integration

`semindex-lsp` communicates over standard input and output. An editor must
start it as a Language Server Protocol process for C and C++ files with the
project root set to the source repository.

The server does not currently provide completion, formatting, or rename. It
can run alongside another language server when the client supports multiple
servers for one document.

## Vim

With [vim-lsp](https://github.com/prabirshrestha/vim-lsp), add a server
registration to `~/.vimrc`:

```vim
if executable('semindex-lsp')
    au User lsp_setup call lsp#register_server({
                \ 'name': 'semindex-lsp',
                \ 'cmd': {server_info->['semindex-lsp']},
                \ 'allowlist': ['c', 'cpp'],
                \ })
endif
```

The editor's detected root should be the same repository root used to create
the index. Start Vim from that root or configure the client's root detection
when relative indexed paths do not resolve correctly.

## Emacs

Recent Emacs versions include
[Eglot](https://www.gnu.org/software/emacs/manual/html_mono/eglot.html). Register
`semindex-lsp` for the traditional and tree-sitter C and C++ modes:

```elisp
(with-eval-after-load 'eglot
  (add-to-list 'eglot-server-programs
               '((c-mode c-ts-mode c++-mode c++-ts-mode)
                 . ("semindex-lsp"))))
```

Open a C source file and run `M-x eglot`. Definition and reference requests are
exposed through the normal Emacs Xref commands, including `M-.` and `M-?`.
Use `M-x eglot-events-buffer` and `M-x eglot-stderr-buffer` to inspect the
client protocol log and server diagnostics.

Eglot normally selects one server program for a major mode in one project.
This registration takes precedence over a later matching C server entry. If
another C language server is required for completion or hover, choose which
server Eglot should manage or use a client setup that supports multiple servers
for one buffer.

## Visual Studio Code

Visual Studio Code connects language servers through extensions rather than a
built-in arbitrary-server setting. One option is the third-party
[Generic LSP Client](https://marketplace.visualstudio.com/items?itemName=zsol.vscode-glspc)
extension. After installing it, add the following workspace settings:

```json
{
    "glspc.server.command": "semindex-lsp",
    "glspc.server.commandArguments": [],
    "glspc.server.languageId": ["c"]
}
```

Start Visual Studio Code from an environment where `semindex-lsp` is in
`PATH`, or set an absolute command path. Open the repository root as the
workspace so the LSP `rootUri` and repository-relative index paths agree.

The Generic LSP Client output channel contains startup errors and optional
protocol tracing. The extension is not maintained or distributed by the
semindex project. A dedicated semindex Visual Studio Code extension does not
currently exist; the official
[language-server extension guide](https://code.visualstudio.com/api/language-extensions/language-server-extension-guide)
describes how such a client extension is built.

## Client diagnostics

Start the server with a protocol log while diagnosing an integration:

```sh
semindex-lsp --logfile=/tmp/semindex-lsp.log
```

The log records timestamped requests and responses and may contain complete
document text. Do not publish it without reviewing sensitive source content.

See [lsp.md](lsp.md) for supported requests, compiler command selection,
incremental indexing, and fallback behavior.
