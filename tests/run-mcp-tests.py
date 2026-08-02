#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def fail(message):
	print(f"FAIL: {message}", file=sys.stderr)
	sys.exit(1)


class Client:
	def __init__(self, executable, database, workspace, logfile, options=None):
		arguments = [
			executable,
			f"--database={database}",
			f"--workspace={workspace}",
			f"--logfile={logfile}",
		]

		if options:
			arguments.extend(options)

		self.process = subprocess.Popen(
			arguments,
			stdin=subprocess.PIPE,
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
			text=True,
		)
		self.next_id = 1

	def send(self, method, params=None, notification=False):
		message = {"jsonrpc": "2.0", "method": method}

		if params is not None:
			message["params"] = params

		if not notification:
			message["id"] = self.next_id
			self.next_id += 1

		self.process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
		self.process.stdin.flush()

		if notification:
			return None

		return self.receive(message["id"], method)

	def request(self, method, params=None):
		message = {"jsonrpc": "2.0", "method": method, "id": self.next_id}

		self.next_id += 1

		if params is not None:
			message["params"] = params

		self.process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
		self.process.stdin.flush()

		return message["id"]

	def receive(self, request_id, method):

		line = self.process.stdout.readline()

		if not line:
			fail(f"server exited while handling {method}: {self.process.stderr.read()}")

		response = json.loads(line)

		if response.get("id") != request_id:
			fail(f"unexpected response ID for {method}: {response}")

		return response

	def receive_any(self, method):
		line = self.process.stdout.readline()

		if not line:
			fail(f"server exited while handling {method}: {self.process.stderr.read()}")

		return json.loads(line)

	def tool(self, name, arguments=None):
		params = {"name": name}

		if arguments is not None:
			params["arguments"] = arguments

		response = self.send("tools/call", params)

		if "error" in response:
			return response

		result = response["result"]

		if result.get("isError"):
			return result

		if "structuredContent" not in result or not result.get("content"):
			fail(f"tool {name} did not return structured and text content")

		return result["structuredContent"]

	def close(self):
		self.process.stdin.close()
		status = self.process.wait(timeout=10)

		if status:
			fail(f"server exited with status {status}: {self.process.stderr.read()}")


def index_fixture(semindex, database, workspace, source, store_command=False):
	arguments = [
			semindex,
			"compiler",
			f"--database={database}",
			"--",
			"cc",
			"--no-default-config",
			source,
		]

	if not store_command:
		arguments.insert(3, "--no-store-command")

	subprocess.run(
		arguments,
		cwd=workspace,
		check=True,
	)


def only_record(result, tool):
	records = result.get("records", [])

	if len(records) != 1:
		fail(f"{tool} returned {len(records)} records instead of one")

	return records[0]


def main():
	if len(sys.argv) != 4:
		fail("usage: run-mcp-tests.py MCP SEMINDEX SOURCE_DIR")

	mcp, semindex, source_dir = sys.argv[1:]
	workspace = Path(source_dir).resolve()

	with tempfile.TemporaryDirectory() as temporary:
		database = Path(temporary) / "semindex.db"
		logfile = Path(temporary) / "mcp.log"

		for source in ("tests/test11.c", "tests/callgraph-a.c", "tests/callgraph-b.c"):
			index_fixture(semindex, database, workspace, source)

		client = Client(mcp, database, workspace, logfile)
		initialize = client.send(
			"initialize",
			{
				"protocolVersion": "2025-11-25",
				"capabilities": {},
				"clientInfo": {"name": "semindex-test", "version": "1"},
			},
		)

		if initialize.get("result", {}).get("protocolVersion") != "2025-11-25":
			fail("initialize did not negotiate the supported protocol")

		client.send("notifications/initialized", notification=True)
		listed = client.send("tools/list", {})
		names = {tool["name"] for tool in listed.get("result", {}).get("tools", [])}
		expected = {
			"search_symbols",
			"symbol_at",
			"find_definitions",
			"find_references",
			"find_callers",
			"find_callees",
			"read_source_context",
			"list_variants",
			"index_status",
		}

		if names != expected:
			fail(f"unexpected tool set: {sorted(names)}")

		variants = client.tool("list_variants", {"limit": 1})

		if [variant["name"] for variant in variants["variants"]] != ["general"]:
			fail("list_variants did not return general")

		first = client.tool("search_symbols", {"pattern": "Outer.y", "limit": 1})
		first_record = only_record(first, "search_symbols first page")

		if not first.get("truncated") or "nextCursor" not in first:
			fail("search_symbols did not paginate a multi-record result")

		second = client.tool(
			"search_symbols",
			{"pattern": "Outer.y", "limit": 1, "cursor": first["nextCursor"]},
		)
		second_record = only_record(second, "search_symbols second page")

		if (first_record["line"], second_record["line"]) != (8, 14):
			fail("search cursor did not preserve stable record order")

		field = client.tool("symbol_at", {"path": "tests/test11.c", "line": 14, "column": 3})
		field_record = only_record(field, "symbol_at field")

		if field_record["symbol"] != "Outer.y" or field_record["action"] != "write":
			fail("symbol_at returned the wrong field operation")

		definitions = client.tool(
			"find_definitions",
			{"symbol": "Outer.y", "variant": "general", "kind": "field"},
		)

		if only_record(definitions, "find_definitions")["line"] != 8:
			fail("find_definitions returned the wrong location")

		references = client.tool(
			"find_references",
			{"symbol": "Outer.y", "variant": "general", "kind": "field"},
		)

		if only_record(references, "find_references")["line"] != 14:
			fail("find_references returned the wrong location")

		context = client.tool(
			"read_source_context",
			{"path": "tests/test11.c", "variant": "general", "firstLine": 12, "lineCount": 3},
		)

		if context.get("origin") != "working-tree" or context.get("lines", [None])[0] != "struct Outer o = {":
			fail("read_source_context returned unexpected source")

		status = client.tool("index_status", {"path": "tests/test11.c", "variant": "general"})

		if status.get("status") != "current" or status.get("drifted"):
			fail("index_status did not report current source")

		caller = only_record(
			client.tool("symbol_at", {"path": "tests/callgraph-a.c", "line": 13, "column": 13}),
			"symbol_at caller",
		)
		callees = client.tool(
			"find_callees",
			{"symbol": "caller", "variant": "general", "usrId": caller["usrId"], "limit": 10},
		)

		if not any(record["symbol"] == "leaf" for record in callees.get("records", [])):
			fail("find_callees omitted leaf")

		leaf = only_record(
			client.tool("symbol_at", {"path": "tests/callgraph-a.c", "line": 7, "column": 6}),
			"symbol_at leaf",
		)
		callers = client.tool(
			"find_callers",
			{"symbol": "leaf", "variant": "general", "usrId": leaf["usrId"], "limit": 10},
		)

		if not any(record["context"] == "caller" for record in callers.get("records", [])):
			fail("find_callers omitted caller")

		entry = only_record(
			client.tool("symbol_at", {"path": "tests/callgraph-a.c", "line": 21, "column": 6}),
			"symbol_at entry_a",
		)
		graph = client.tool(
			"find_callees",
			{
				"symbol": "entry_a",
				"variant": "general",
				"usrId": entry["usrId"],
				"depth": 2,
				"nodeLimit": 10,
				"limit": 20,
			},
		)
		depths = {(record["symbol"], record["depth"]) for record in graph.get("records", [])}

		if ("caller", 1) not in depths or ("leaf", 2) not in depths:
			fail("recursive find_callees returned the wrong depths")

		if graph.get("truncated") or not graph.get("cyclesDetected"):
			fail("recursive find_callees did not report its cycle")

		reverse_graph = client.tool(
			"find_callers",
			{
				"symbol": "leaf",
				"variant": "general",
				"usrId": leaf["usrId"],
				"depth": 2,
				"nodeLimit": 10,
				"limit": 20,
			},
		)
		reverse_contexts = {
			(record["context"], record["path"], record["depth"])
			for record in reverse_graph.get("records", [])
		}

		if ("entry_a", "tests/callgraph-a.c", 2) not in reverse_contexts:
			fail("recursive find_callers omitted entry_a")

		if any(path == "tests/callgraph-b.c" for _, path, _ in reverse_contexts):
			fail("recursive find_callers mixed static function identities")

		bounded = client.tool(
			"find_callees",
			{
				"symbol": "entry_a",
				"variant": "general",
				"usrId": entry["usrId"],
				"depth": 2,
				"nodeLimit": 2,
				"limit": 20,
			},
		)

		if not bounded.get("truncated") or not bounded.get("nodeLimitHit"):
			fail("recursive find_callees did not enforce its node limit")

		record_bounded = client.tool(
			"find_callees",
			{
				"symbol": "entry_a",
				"variant": "general",
				"usrId": entry["usrId"],
				"depth": 2,
				"nodeLimit": 10,
				"limit": 1,
			},
		)

		if len(record_bounded.get("records", [])) != 1 or not record_bounded.get("truncated"):
			fail("recursive find_callees did not enforce its record limit")

		invalid_cursor = client.tool(
			"find_callees",
			{
				"symbol": "entry_a",
				"variant": "general",
				"usrId": entry["usrId"],
				"depth": 2,
				"cursor": "not-supported",
			},
		)

		if not invalid_cursor.get("isError"):
			fail("recursive find_callees accepted a cursor")

		outside = client.tool("read_source_context", {"path": "../etc/passwd", "variant": "general"})

		if not outside.get("isError"):
			fail("source access escaped the workspace")

		unknown = client.tool("not_a_tool", {})

		if unknown.get("error", {}).get("code") != -32602:
			fail("unknown tool did not return Invalid Params")

		client.close()

		log = logfile.read_text()

		if "CLIENT --> SERVER" not in log or "SERVER --> CLIENT" not in log:
			fail("protocol logfile did not contain both directions")

		update_workspace = Path(temporary) / "workspace"
		update_workspace.mkdir()
		update_database = Path(temporary) / "update.db"
		update_log = Path(temporary) / "update.log"
		update_source = update_workspace / "update.c"
		update_source.write_text("int before(void) { return 0; }\n", encoding="utf-8")
		index_fixture(semindex, update_database, update_workspace, "update.c", store_command=True)

		update_client = Client(mcp, update_database, update_workspace, update_log, ["--allow-reindex"])
		update_client.send(
			"initialize",
			{
				"protocolVersion": "2025-11-25",
				"capabilities": {},
				"clientInfo": {"name": "semindex-update-test", "version": "1"},
			},
		)
		update_client.send("notifications/initialized", notification=True)
		update_tools = update_client.send("tools/list", {})
		update_names = {tool["name"] for tool in update_tools.get("result", {}).get("tools", [])}

		if "reindex_file" not in update_names:
			fail("opt-in MCP server did not advertise reindex_file")

		reindex_definition = next(tool for tool in update_tools["result"]["tools"] if tool["name"] == "reindex_file")

		if reindex_definition["annotations"].get("readOnlyHint"):
			fail("reindex_file was advertised as read-only")

		update_status = update_client.tool("index_status", {"path": "update.c", "variant": "general"})

		if not update_status.get("compilerCommandAvailable"):
			fail("index_status did not find the saved compiler command")

		update_source.write_text("int after(void) { return missing; }\n", encoding="utf-8")
		partial = update_client.tool("reindex_file", {"path": "update.c", "variant": "general"})

		if partial.get("status") != "partial" or not partial.get("diagnostics"):
			fail("reindex_file did not return partial diagnostics")

		partial_records = update_client.tool("search_symbols", {"pattern": "after", "variant": "general"})

		if not partial_records.get("records"):
			fail("reindex_file did not store the partial index")

		update_source.write_text("int after(void) { return 0; }\n", encoding="utf-8")
		clean = update_client.tool("reindex_file", {"path": "update.c", "variant": "general"})

		if clean.get("status") != "clean" or clean.get("diagnostics"):
			fail("reindex_file did not return a clean status")

		missing_command = update_workspace / "missing-command.c"
		missing_command.write_text("int missing_command;\n", encoding="utf-8")
		failed = update_client.tool("reindex_file", {"path": "missing-command.c", "variant": "general"})

		if failed.get("status") != "failed" or failed.get("compilerCommandAvailable"):
			fail("reindex_file did not report a missing compiler command")

		second_source = update_workspace / "update-two.c"
		second_source.write_text("int second_before(void) { return 0; }\n", encoding="utf-8")
		index_fixture(semindex, update_database, update_workspace, "update-two.c", store_command=True)
		update_source.write_text("int concurrent_one(void) { return 0; }\n", encoding="utf-8")
		second_source.write_text("int concurrent_two(void) { return 0; }\n", encoding="utf-8")
		first_id = update_client.request(
			"tools/call",
			{"name": "reindex_file", "arguments": {"path": "update.c", "variant": "general"}},
		)
		second_id = update_client.request(
			"tools/call",
			{"name": "reindex_file", "arguments": {"path": "update-two.c", "variant": "general"}},
		)
		concurrent = [
			update_client.receive_any("concurrent reindex_file"),
			update_client.receive_any("concurrent reindex_file"),
		]

		if {response.get("id") for response in concurrent} != {first_id, second_id}:
			fail(f"concurrent reindex_file returned unexpected IDs: {concurrent}")

		for response in concurrent:
			content = response.get("result", {}).get("structuredContent", {})

			if content.get("status") != "clean":
				fail(f"concurrent reindex_file failed: {response}")

		update_client.close()


if __name__ == "__main__":
	main()
