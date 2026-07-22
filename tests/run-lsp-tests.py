#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import sqlite3
import subprocess
import sys
import tempfile
from datetime import datetime
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


def function_id(database, source, symbol):
    with sqlite3.connect(database) as connection:
        row = connection.execute(
            "SELECT records.usr_id FROM records "
            "JOIN files ON files.id = records.file_id "
            "WHERE files.variant = 'general' AND files.path = ? "
            "AND records.symbol = ? AND records.record = 0 "
            "AND records.action != 0 AND records.kind = 7",
            (str(source), symbol),
        ).fetchone()
    if not row or row[0] is None:
        fail(f"missing function ID for {source}:{symbol}")
    return f"{row[0] & ((1 << 64) - 1):016x}"


def hierarchy_item(source, name, line, character, identity):
    selection = {
        "start": {"line": line, "character": character},
        "end": {"line": line, "character": character + len(name)},
    }
    return {
        "name": name,
        "kind": 12,
        "detail": str(source),
        "uri": source.resolve().as_uri(),
        "range": selection,
        "selectionRange": selection,
        "data": {
            "variant": "general",
            "symbol": name,
            "id": identity,
        },
    }


def source_range(line, character, length):
    return {
        "start": {"line": line, "character": character},
        "end": {"line": line, "character": character + length},
    }


def main():
    if len(sys.argv) != 3:
        fail("usage: run-lsp-tests.py SEMINDEX_LSP SEMINDEX")

    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        source = directory / "navigation.c"
        database = directory / "semindex.db"
        commands_database = directory / "commands.db"
        logfile = directory / "semindex-lsp.log"
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
                f"--commands-database={commands_database}", "--", "cc",
                source.name,
            ],
            cwd=directory,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if indexed.returncode != 0:
            fail("failed to create the LSP test index", indexed)

        macro_source = directory / "macro-call.c"
        macro_source.write_text(
            "int target(int);\n"
            "#define CALL_TARGET(value) target(value)\n"
            "int invoke(int value) { return CALL_TARGET(value); }\n",
            encoding="utf-8",
        )
        indexed = subprocess.run(
            [
                sys.argv[2], "compiler", f"--database={database}",
                "--no-store-command", "--", "cc", macro_source.name,
            ],
            cwd=directory,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if indexed.returncode != 0:
            fail("failed to create the macro call index", indexed)

        with sqlite3.connect(database) as connection:
            plan = connection.execute(
                "EXPLAIN QUERY PLAN SELECT files.variant, files.path, "
                "records.line, records.column FROM files CROSS JOIN records "
                "ON records.file_id = files.id WHERE files.path = ? "
                "AND records.line = ? AND records.column <= ? "
                "ORDER BY records.column DESC, records.record",
                (macro_source.name, 2, 32),
            ).fetchall()
        plan_details = "\n".join(row[3] for row in plan)
        if "SCAN records" in plan_details or (
            "records_file_idx (file_id=?)" not in plan_details
        ):
            fail(f"cursor lookup does not use the file index:\n{plan_details}")

        callgraph_a = Path(__file__).resolve().parent / "callgraph-a.c"
        callgraph_b = Path(__file__).resolve().parent / "callgraph-b.c"
        for callgraph_source in (callgraph_a, callgraph_b):
            indexed = subprocess.run(
                [
                    sys.argv[2], "compiler", f"--database={database}",
                    "--no-store-command", "--", "cc", str(callgraph_source),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if indexed.returncode != 0:
                fail(f"failed to index {callgraph_source.name}", indexed)

        caller_a = hierarchy_item(
            callgraph_a, "caller", 12, 12,
            function_id(database, callgraph_a, "caller"),
        )
        helper_a = hierarchy_item(
            callgraph_a, "helper", 2, 12,
            function_id(database, callgraph_a, "helper"),
        )
        leaf = hierarchy_item(
            callgraph_a, "leaf", 6, 5,
            function_id(database, callgraph_a, "leaf"),
        )

        options = [
            f"--database={database}",
            f"--commands-database={commands_database}",
            "--variant=general",
            f"--logfile={logfile}",
        ]
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
                {
                    "jsonrpc": "2.0", "id": 20,
                    "method": "textDocument/prepareCallHierarchy",
                    "params": {
                        "textDocument": {"uri": callgraph_a.resolve().as_uri()},
                        "position": {"line": 12, "character": 14},
                    },
                },
                {
                    "jsonrpc": "2.0", "id": 21,
                    "method": "callHierarchy/outgoingCalls",
                    "params": {"item": caller_a},
                },
                {
                    "jsonrpc": "2.0", "id": 22,
                    "method": "callHierarchy/incomingCalls",
                    "params": {"item": helper_a},
                },
                {"jsonrpc": "2.0", "id": "missing", "method": "unknown"},
                {"jsonrpc": "2.0", "id": 2, "method": "shutdown"},
                {"jsonrpc": "2.0", "id": 4, "method": "exit"},
                {"jsonrpc": "2.0", "method": "exit"},
            ],
        )
        if process.returncode != 0:
            fail(f"normal lifecycle exited with status {process.returncode}", process)
        if len(responses) != 12:
            fail(f"normal lifecycle returned {len(responses)} responses", process)
        if responses[0].get("error", {}).get("code") != -32700:
            fail("malformed JSON did not produce a parse error")
        initialize = responses[1]
        capabilities = initialize.get("result", {}).get("capabilities", {})
        if initialize.get("id") != 1 or capabilities.get(
            "positionEncoding"
        ) != "utf-16" or capabilities.get("definitionProvider") is not True or (
            capabilities.get("referencesProvider") is not True
        ) or capabilities.get("textDocumentSync") != {
            "change": 0, "save": True,
        } or capabilities.get("callHierarchyProvider") is not True:
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
        if responses[6].get("result") != [caller_a]:
            fail("prepareCallHierarchy returned an unexpected item")
        expected_outgoing = [
            {
                "to": caller_a,
                "fromRanges": [source_range(17, 1, len("caller"))],
            },
            {
                "to": helper_a,
                "fromRanges": [source_range(14, 1, len("helper"))],
            },
            {
                "to": leaf,
                "fromRanges": [
                    source_range(15, 1, len("leaf")),
                    source_range(16, 8, len("leaf")),
                ],
            },
        ]
        if responses[7].get("result") != expected_outgoing:
            fail(f"outgoing calls differ: {responses[7].get('result')}")
        expected_incoming = [{
            "from": caller_a,
            "fromRanges": [source_range(14, 1, len("helper"))],
        }]
        if responses[8].get("result") != expected_incoming:
            fail(f"incoming calls differ: {responses[8].get('result')}")
        if responses[9].get("id") != "missing" or responses[9].get(
            "error", {}
        ).get("code") != -32601:
            fail("unknown request did not produce MethodNotFound")
        if responses[10] != {"id": 2, "jsonrpc": "2.0", "result": None}:
            fail("shutdown response is malformed")
        if responses[11].get("id") != 4 or responses[11].get("error", {}).get(
            "code"
        ) != -32600:
            fail("exit request was accepted")

        protocol_log = logfile.read_text(encoding="utf-8")
        first_marker = protocol_log.splitlines()[0]
        timestamp, marker = first_marker.split(" ", 1)
        try:
            datetime.strptime(timestamp, "%Y-%m-%dT%H:%M:%S.%fZ")
        except ValueError as error:
            fail(f"protocol log timestamp is invalid: {error}")
        if marker != "CLIENT --> SERVER":
            fail("protocol log starts with an unexpected marker")
        if protocol_log.count("CLIENT --> SERVER\n") != 14:
            fail("protocol log has an unexpected request count")
        if protocol_log.count("SERVER --> CLIENT\n") != len(responses):
            fail("protocol log has an unexpected response count")
        if '"method":"textDocument/definition"' not in protocol_log or (
            '"id":5' not in protocol_log
        ):
            fail("protocol log is missing an incoming request")
        if '"id":2,"jsonrpc":"2.0","result":null' not in protocol_log:
            fail("protocol log is missing an outgoing response")

        process = subprocess.run(
            [sys.argv[1], "--logfile="],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if process.returncode == 0 or (
            b"empty log file path" not in process.stderr
        ):
            fail("empty protocol log path was accepted", process)

        process, responses = run_server(sys.argv[1], options, [
            {
                "jsonrpc": "2.0", "id": 30, "method": "initialize",
                "params": {
                    "rootUri": None,
                    "workspaceFolders": [{
                        "uri": root_uri,
                        "name": directory.name,
                    }],
                },
            },
            {
                "jsonrpc": "2.0", "id": 31,
                "method": "textDocument/references",
                "params": {
                    "textDocument": {"uri": uri},
                    "position": {
                        "line": 5, "character": reference_character,
                    },
                    "context": {"includeDeclaration": False},
                },
            },
            {"jsonrpc": "2.0", "id": 32, "method": "shutdown"},
            {"jsonrpc": "2.0", "method": "exit"},
        ])
        if process.returncode != 0:
            fail("workspaceFolders lifecycle failed", process)
        if len(responses) != 3 or responses[1].get("result") != [
            expected_reference
        ]:
            fail("workspaceFolders root did not resolve relative index paths")

        macro_uri = macro_source.resolve().as_uri()
        process, responses = run_server(sys.argv[1], options, [
            {
                "jsonrpc": "2.0", "id": 40, "method": "initialize",
                "params": {"rootUri": root_uri},
            },
            {
                "jsonrpc": "2.0", "id": 41,
                "method": "textDocument/references",
                "params": {
                    "textDocument": {"uri": macro_uri},
                    "position": {"line": 1, "character": 30},
                    "context": {"includeDeclaration": False},
                },
            },
            {"jsonrpc": "2.0", "id": 42, "method": "shutdown"},
            {"jsonrpc": "2.0", "method": "exit"},
        ])
        expected_macro_reference = {
            "uri": macro_uri,
            "range": source_range(1, 27, len("target")),
        }
        if process.returncode != 0:
            fail("macro call lifecycle failed", process)
        if len(responses) != 3 or responses[1].get("result") != [
            expected_macro_reference
        ]:
            fail("macro body call did not resolve at its spelling location")

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

        source.write_text(
            "struct Nav {\n"
            "\tint renamed;\n"
            "};\n"
            "int read_field(struct Nav *p)\n"
            "{\n"
            "\treturn /* π */ p->renamed;\n"
            "}\n",
            encoding="utf-8",
        )
        updated_line = source.read_text(encoding="utf-8").splitlines()[5]
        updated_character = len(
            updated_line[:updated_line.index("renamed")].encode("utf-16-le")
        ) // 2
        process, responses = run_server(sys.argv[1], options, [
            {
                "jsonrpc": "2.0", "id": 10, "method": "initialize",
                "params": {"rootUri": root_uri},
            },
            {"jsonrpc": "2.0", "method": "initialized", "params": {}},
            {
                "jsonrpc": "2.0", "method": "textDocument/didSave",
                "params": {"textDocument": {"uri": uri}},
            },
            {
                "jsonrpc": "2.0", "id": 11,
                "method": "textDocument/definition",
                "params": {
                    "textDocument": {"uri": uri},
                    "position": {
                        "line": 5, "character": updated_character + 6,
                    },
                },
            },
            {
                "jsonrpc": "2.0", "id": 12,
                "method": "textDocument/references",
                "params": {
                    "textDocument": {"uri": uri},
                    "position": {
                        "line": 5, "character": updated_character,
                    },
                    "context": {"includeDeclaration": False},
                },
            },
            {"jsonrpc": "2.0", "id": 13, "method": "shutdown"},
            {"jsonrpc": "2.0", "method": "exit"},
        ])
        if process.returncode != 0:
            fail(f"didSave lifecycle exited with status {process.returncode}", process)
        if len(responses) != 4:
            fail(f"didSave lifecycle returned {len(responses)} responses", process)
        expected_updated_definition = {
            "uri": uri,
            "range": {
                "start": {"line": 1, "character": 5},
                "end": {"line": 1, "character": 12},
            },
        }
        if responses[1].get("result") != [expected_updated_definition]:
            fail("didSave did not update the definition")
        expected_updated_reference = {
            "uri": uri,
            "range": {
                "start": {"line": 5, "character": updated_character},
                "end": {"line": 5, "character": updated_character + 7},
            },
        }
        if responses[2].get("result") != [expected_updated_reference]:
            fail("didSave did not update references")
        if responses[3] != {"id": 13, "jsonrpc": "2.0", "result": None}:
            fail("didSave shutdown response is malformed")

        with sqlite3.connect(database) as connection:
            local_records = connection.execute(
                "SELECT count(*) FROM records JOIN files "
                "ON files.id = records.file_id WHERE files.path = ? "
                "AND records.local != 0",
                (source.name,),
            ).fetchone()[0]
        if not local_records:
            fail("LSP omitted local records by default")

        process, responses = run_server(
            sys.argv[1], [*options, "--no-include-local"], [
                {
                    "jsonrpc": "2.0", "id": 50, "method": "initialize",
                    "params": {"rootUri": root_uri},
                },
                {
                    "jsonrpc": "2.0", "method": "textDocument/didSave",
                    "params": {"textDocument": {"uri": uri}},
                },
                {"jsonrpc": "2.0", "id": 51, "method": "shutdown"},
                {"jsonrpc": "2.0", "method": "exit"},
            ],
        )
        if process.returncode != 0 or len(responses) != 2:
            fail("LSP --no-include-local lifecycle failed", process)
        with sqlite3.connect(database) as connection:
            local_records = connection.execute(
                "SELECT count(*) FROM records JOIN files "
                "ON files.id = records.file_id WHERE files.path = ? "
                "AND records.local != 0",
                (source.name,),
            ).fetchone()[0]
        if local_records:
            fail("LSP --no-include-local retained local records")


if __name__ == "__main__":
    main()
