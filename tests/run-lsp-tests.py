#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def fail(message, process=None):
    if process and process.stderr:
        sys.stderr.buffer.write(process.stderr)
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def frame(message):
    payload = message if isinstance(message, bytes) else json.dumps(
        message, separators=(",", ":")
    ).encode()
    return f"Content-Length: {len(payload)}\r\n\r\n".encode() + payload


def parse_messages(data):
    messages = []
    while data:
        header, separator, rest = data.partition(b"\r\n\r\n")
        if not separator:
            fail("response has an incomplete header")
        fields = {}
        for line in header.split(b"\r\n"):
            name, separator, value = line.partition(b":")
            if not separator:
                fail("response has a malformed header")
            fields[name.lower()] = value.strip()
        try:
            length = int(fields[b"content-length"])
        except (KeyError, ValueError):
            fail("response has an invalid Content-Length header")
        payload, data = rest[:length], rest[length:]
        if len(payload) != length:
            fail("response has a truncated body")
        messages.append(json.loads(payload))
    return messages


def run_server(binary, options, messages):
    process = subprocess.run(
        [binary, *options],
        input=b"".join(frame(message) for message in messages),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    return process, parse_messages(process.stdout)


def main():
    if len(sys.argv) != 3:
        fail("usage: run-lsp-tests.py SEMINDEX_LSP SEMINDEX")

    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        source = directory / "navigation.c"
        database = directory / "semindex.db"
        source.write_text(
            "struct Nav {\n"
            "\tint field;\n"
            "};\n"
            "int read_field(struct Nav *p)\n"
            "{\n"
            "\treturn /* π */ p->field;\n"
            "}\n",
            encoding="utf-8",
        )
        indexed = subprocess.run(
            [
                sys.argv[2], "compiler", f"--database={database}",
                "--no-store-command", "--", "cc", source.name,
            ],
            cwd=directory,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if indexed.returncode != 0:
            fail("failed to create the LSP test index", indexed)

        options = [f"--database={database}", "--variant=general"]
        uri = source.resolve().as_uri()
        root_uri = directory.resolve().as_uri()
        reference_line = source.read_text(encoding="utf-8").splitlines()[5]
        reference_character = len(
            reference_line[:reference_line.index("field")].encode("utf-16-le")
        ) // 2

        process, responses = run_server(
            sys.argv[1], options, [
                b"{",
                {
                    "jsonrpc": "2.0", "id": 1, "method": "initialize",
                    "params": {"rootUri": root_uri},
                },
                {"jsonrpc": "2.0", "method": "initialized", "params": {}},
                {
                    "jsonrpc": "2.0", "id": 3, "method": "initialized",
                    "params": {},
                },
                {
                    "jsonrpc": "2.0", "id": 5,
                    "method": "textDocument/definition",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {
                            "line": 5, "character": reference_character + 2,
                        },
                    },
                },
                {
                    "jsonrpc": "2.0", "id": 6,
                    "method": "textDocument/references",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {
                            "line": 5, "character": reference_character,
                        },
                        "context": {"includeDeclaration": False},
                    },
                },
                {
                    "jsonrpc": "2.0", "id": 7,
                    "method": "textDocument/references",
                    "params": {
                        "textDocument": {"uri": uri},
                        "position": {
                            "line": 5, "character": reference_character,
                        },
                        "context": {"includeDeclaration": True},
                    },
                },
                {"jsonrpc": "2.0", "id": "missing", "method": "unknown"},
                {"jsonrpc": "2.0", "id": 2, "method": "shutdown"},
                {"jsonrpc": "2.0", "id": 4, "method": "exit"},
                {"jsonrpc": "2.0", "method": "exit"},
            ],
        )
        if process.returncode != 0:
            fail(f"normal lifecycle exited with status {process.returncode}", process)
        if len(responses) != 9:
            fail(f"normal lifecycle returned {len(responses)} responses", process)
        if responses[0].get("error", {}).get("code") != -32700:
            fail("malformed JSON did not produce a parse error")
        initialize = responses[1]
        capabilities = initialize.get("result", {}).get("capabilities", {})
        if initialize.get("id") != 1 or capabilities.get(
            "positionEncoding"
        ) != "utf-16" or capabilities.get("definitionProvider") is not True or (
            capabilities.get("referencesProvider") is not True
        ):
            fail("initialize response has unexpected capabilities")
        if responses[2].get("id") != 3 or responses[2].get("error", {}).get(
            "code"
        ) != -32600:
            fail("initialized request was accepted")

        definition = responses[3].get("result")
        expected_definition = {
            "uri": uri,
            "range": {
                "start": {"line": 1, "character": 5},
                "end": {"line": 1, "character": 10},
            },
        }
        if definition != [expected_definition]:
            fail(f"definition returned unexpected locations: {definition}")
        expected_reference = {
            "uri": uri,
            "range": {
                "start": {"line": 5, "character": reference_character},
                "end": {"line": 5, "character": reference_character + 5},
            },
        }
        if responses[4].get("result") != [expected_reference]:
            fail("references returned unexpected locations")
        if responses[5].get("result") != [expected_reference, expected_definition]:
            fail("references did not include the declaration")
        if responses[6].get("id") != "missing" or responses[6].get(
            "error", {}
        ).get("code") != -32601:
            fail("unknown request did not produce MethodNotFound")
        if responses[7] != {"id": 2, "jsonrpc": "2.0", "result": None}:
            fail("shutdown response is malformed")
        if responses[8].get("id") != 4 or responses[8].get("error", {}).get(
            "code"
        ) != -32600:
            fail("exit request was accepted")

        process, responses = run_server(sys.argv[1], options, [
            {"jsonrpc": "2.0", "method": "exit"},
        ])
        if process.returncode == 0 or responses:
            fail("exit before shutdown was accepted", process)

        process = subprocess.run(
            [sys.argv[1], *options],
            input=b"Content-Length: invalid\r\n\r\n",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if process.returncode == 0 or process.stdout:
            fail("invalid framing was accepted", process)


if __name__ == "__main__":
    main()
