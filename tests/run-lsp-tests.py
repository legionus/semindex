#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import subprocess
import sys


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


def run_server(binary, messages):
    process = subprocess.run(
        [binary],
        input=b"".join(frame(message) for message in messages),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    return process, parse_messages(process.stdout)


def main():
    if len(sys.argv) != 2:
        fail("usage: run-lsp-tests.py SEMINDEX_LSP")

    process, responses = run_server(sys.argv[1], [
        b"{",
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "id": 3, "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "id": "missing", "method": "unknown"},
        {"jsonrpc": "2.0", "id": 2, "method": "shutdown"},
        {"jsonrpc": "2.0", "id": 4, "method": "exit"},
        {"jsonrpc": "2.0", "method": "exit"},
    ])
    if process.returncode != 0:
        fail(f"normal lifecycle exited with status {process.returncode}", process)
    if len(responses) != 6:
        fail(f"normal lifecycle returned {len(responses)} responses", process)
    if responses[0].get("error", {}).get("code") != -32700:
        fail("malformed JSON did not produce a parse error")
    initialize = responses[1]
    if initialize.get("id") != 1 or initialize.get("result", {}).get(
        "capabilities", {}
    ).get("positionEncoding") != "utf-16":
        fail("initialize response has unexpected capabilities")
    if responses[2].get("id") != 3 or responses[2].get("error", {}).get(
        "code"
    ) != -32600:
        fail("initialized request was accepted")
    if responses[3].get("id") != "missing" or responses[3].get(
        "error", {}
    ).get("code") != -32601:
        fail("unknown request did not produce MethodNotFound")
    if responses[4] != {"id": 2, "jsonrpc": "2.0", "result": None}:
        fail("shutdown response is malformed")
    if responses[5].get("id") != 4 or responses[5].get("error", {}).get(
        "code"
    ) != -32600:
        fail("exit request was accepted")

    process, responses = run_server(sys.argv[1], [
        {"jsonrpc": "2.0", "method": "exit"},
    ])
    if process.returncode == 0 or responses:
        fail("exit before shutdown was accepted", process)

    process = subprocess.run(
        [sys.argv[1]],
        input=b"Content-Length: invalid\r\n\r\n",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if process.returncode == 0 or process.stdout:
        fail("invalid framing was accepted", process)


if __name__ == "__main__":
    main()
