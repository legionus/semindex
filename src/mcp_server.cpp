// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp_server.h"

#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

#include <limits>
#include <optional>
#include <thread>
#include <utility>

static constexpr llvm::StringLiteral PROTOCOL_VERSION = "2025-11-25";
static constexpr size_t MAX_RESPONSE_SIZE = 1024 * 1024;
static constexpr int PARSE_ERROR = -32700;
static constexpr int INVALID_REQUEST = -32600;
static constexpr int METHOD_NOT_FOUND = -32601;
static constexpr int INVALID_PARAMS = -32602;
static constexpr int INTERNAL_ERROR = -32603;
static constexpr int SERVER_NOT_INITIALIZED = -32002;
static constexpr int REQUEST_CANCELLED = -32800;
static constexpr int DEADLINE_EXCEEDED = -32001;

namespace
{

llvm::json::Object stringProperty(llvm::StringRef description)
{
	return llvm::json::Object{
		{ "type", "string" },
		{ "description", description },
	};
}

llvm::json::Object integerProperty(llvm::StringRef description, int minimum, int maximum)
{
	return llvm::json::Object{
		{ "type", "integer" },
		{ "description", description },
		{ "minimum", minimum },
		{ "maximum", maximum },
	};
}

llvm::json::Object objectSchema(llvm::json::Object properties, std::initializer_list<const char *> required = {})
{
	llvm::json::Object schema{
		{ "type", "object" },
		{ "properties", std::move(properties) },
		{ "additionalProperties", false },
	};

	if (required.size()) {
		llvm::json::Array names;

		for (const char *name : required)
			names.push_back(name);

		schema["required"] = std::move(names);
	}

	return schema;
}

llvm::json::Object tool(llvm::StringRef name, llvm::StringRef description, llvm::json::Object schema,
	bool read_only = true)
{
	return llvm::json::Object{
		{ "name", name },
		{ "description", description },
		{ "inputSchema", std::move(schema) },
		{ "annotations",
			llvm::json::Object{
				{ "readOnlyHint", read_only },
				{ "destructiveHint", !read_only },
				{ "idempotentHint", true },
			} },
	};
}

llvm::json::Object pagingProperties()
{
	return llvm::json::Object{
		{ "limit", integerProperty("Maximum number of results", 1, 200) },
		{ "cursor", stringProperty("Opaque cursor returned by the previous call") },
	};
}

llvm::json::Object identityProperties()
{
	llvm::json::Object properties = pagingProperties();

	properties["symbol"] = stringProperty("Qualified symbol name");
	properties["variant"] = stringProperty("Index variant");
	properties["usrId"] = stringProperty("Stable hexadecimal symbol identity");
	properties["kind"] = stringProperty("Symbol kind returned by symbol_at");
	properties["context"] = stringProperty("Containing function for a local symbol");
	properties["local"] = llvm::json::Object{ { "type", "boolean" } };

	return properties;
}

llvm::json::Value toolResult(McpToolResult result)
{
	if (result.status != McpToolStatus::Success) {
		return llvm::json::Object{
			{ "content",
				llvm::json::Array{
					llvm::json::Object{
						{ "type", "text" },
						{ "text", result.message },
					},
				} },
			{ "isError", true },
		};
	}

	llvm::json::Value structured(std::move(result.data));
	std::string text;
	llvm::raw_string_ostream stream(text);

	stream << structured;
	stream.flush();

	if (text.size() > MAX_RESPONSE_SIZE) {
		return llvm::json::Object{
			{ "content",
				llvm::json::Array{
					llvm::json::Object{
						{ "type", "text" },
						{ "text", "Response size limit exceeded" },
					},
				} },
			{ "isError", true },
		};
	}

	return llvm::json::Object{
		{ "content",
			llvm::json::Array{
				llvm::json::Object{
					{ "type", "text" },
					{ "text", text },
				},
			} },
		{ "structuredContent", std::move(structured) },
		{ "isError", false },
	};
}

} // namespace

McpServer::McpServer(McpTransport &transport, McpToolService &tools, std::chrono::milliseconds request_timeout)
    : transport(transport), tools(tools), request_timeout(request_timeout)
{
}

std::string McpServer::RequestId::key() const
{
	return numeric ? "n:" + std::to_string(number) : "s:" + text;
}

llvm::json::Value McpServer::RequestId::value() const
{
	return numeric ? llvm::json::Value(number) : llvm::json::Value(text);
}

std::optional<McpServer::RequestId> McpServer::requestId(const llvm::json::Value *id)
{
	if (!id)
		return std::nullopt;

	if (auto number = id->getAsInteger())
		return RequestId{ true, *number, {} };

	if (auto text = id->getAsString())
		return RequestId{ false, 0, text->str() };

	return std::nullopt;
}

llvm::json::Array McpServer::toolDefinitions(bool allow_reindex)
{
	llvm::json::Array result;
	llvm::json::Object search = pagingProperties();

	search["pattern"] = stringProperty("Exact symbol name or SQLite GLOB pattern");
	search["variant"] = stringProperty("Index variant or GLOB pattern");
	search["kind"] = stringProperty("Optional symbol kind");
	result.push_back(tool("search_symbols", "Search bounded semantic index records",
		objectSchema(std::move(search), { "pattern" })));

	llvm::json::Object position{
		{ "path", stringProperty("Source path below the configured workspace") },
		{ "line", integerProperty("One-based source line", 1, std::numeric_limits<int>::max()) },
		{ "column", integerProperty("One-based UTF-8 byte column", 1, std::numeric_limits<int>::max()) },
		{ "variant", stringProperty("Optional index variant") },
	};

	result.push_back(tool("symbol_at", "Resolve semantic identities at a source position",
		objectSchema(std::move(position), { "path", "line", "column" })));
	result.push_back(tool("find_definitions", "Find bounded definitions for a semantic identity",
		objectSchema(identityProperties(), { "symbol" })));
	result.push_back(tool("find_references", "Find bounded references for a semantic identity",
		objectSchema(identityProperties(), { "symbol" })));
	llvm::json::Object declared_types = pagingProperties();

	declared_types["symbol"] = stringProperty("Qualified symbol name");
	declared_types["variant"] = stringProperty("Index variant");
	declared_types["usrId"] = stringProperty("Stable hexadecimal symbol identity");
	declared_types["kind"] = stringProperty("Symbol kind returned by symbol_at");
	result.push_back(tool("find_declared_types", "Find declared C types for a semantic identity",
		objectSchema(std::move(declared_types), { "symbol", "usrId", "kind" })));

	llvm::json::Object calls = pagingProperties();

	calls["symbol"] = stringProperty("Qualified function name");
	calls["variant"] = stringProperty("Index variant");
	calls["usrId"] = stringProperty("Stable hexadecimal function identity");
	calls["depth"] = integerProperty("Maximum traversal depth", 1, 16);
	calls["nodeLimit"] = integerProperty("Maximum unique functions", 1, 200);
	result.push_back(tool("find_callers", "Find bounded callers of a function",
		objectSchema(llvm::json::Object(calls), { "symbol", "usrId" })));
	result.push_back(tool("find_callees", "Find bounded callees of a function",
		objectSchema(std::move(calls), { "symbol", "usrId" })));

	llvm::json::Object source{
		{ "path", stringProperty("Indexed source path below the configured workspace") },
		{ "variant", stringProperty("Index variant") },
		{ "firstLine", integerProperty("First one-based line", 1, std::numeric_limits<int>::max()) },
		{ "lineCount", integerProperty("Maximum number of complete lines", 1, 200) },
		{ "byteLimit", integerProperty("Maximum returned source bytes", 1, 64 * 1024) },
	};

	result.push_back(tool("read_source_context", "Read bounded source matching indexed locations",
		objectSchema(std::move(source), { "path" })));
	result.push_back(
		tool("list_variants", "List index variants and source provenance", objectSchema(pagingProperties())));

	llvm::json::Object status{
		{ "path", stringProperty("Indexed source path below the configured workspace") },
		{ "variant", stringProperty("Index variant") },
	};

	result.push_back(tool("index_status", "Report whether indexed source matches the working tree",
		objectSchema(std::move(status), { "path" })));

	if (allow_reindex) {
		llvm::json::Object reindex{
			{ "path", stringProperty("Source path below the configured workspace") },
			{ "variant", stringProperty("Index variant") },
		};

		result.push_back(tool("reindex_file", "Reindex one file using its saved compiler command",
			objectSchema(std::move(reindex), { "path" }), false));
	}

	return result;
}

bool McpServer::reply(const RequestId &id, llvm::json::Value result)
{
	return transport.write(llvm::json::Object{
		{ "jsonrpc", "2.0" },
		{ "id", id.value() },
		{ "result", std::move(result) },
	});
}

bool McpServer::error(const RequestId *id, int code, llvm::StringRef message)
{
	return transport.write(llvm::json::Object{
		{ "jsonrpc", "2.0" },
		{ "id", id ? id->value() : llvm::json::Value(nullptr) },
		{ "error",
			llvm::json::Object{
				{ "code", code },
				{ "message", message },
			} },
	});
}

bool McpServer::initialize(const RequestId &id, const llvm::json::Object *params)
{
	if (state != State::Uninitialized || !params)
		return error(&id, INVALID_REQUEST, "Invalid initialize request");

	auto version = params->getString("protocolVersion");

	if (!version || !params->getObject("capabilities") || !params->getObject("clientInfo"))
		return error(&id, INVALID_PARAMS, "Missing protocol version");

	state = State::AwaitingInitialized;
	std::string negotiated = *version == PROTOCOL_VERSION ? version->str() : PROTOCOL_VERSION.str();

	return reply(id,
		llvm::json::Object{
			{ "protocolVersion", negotiated },
			{ "capabilities",
				llvm::json::Object{
					{ "tools", llvm::json::Object{ { "listChanged", false } } },
				} },
			{ "serverInfo",
				llvm::json::Object{
					{ "name", "semindex" },
					{ "title", "semindex semantic index" },
					{ "version", "0" },
				} },
			{ "instructions",
				tools.canReindex()
					? "Use bounded tools to inspect the index and reindex individual workspace "
					  "files."
					: "Use bounded read-only tools to inspect the configured semantic index." },
		});
}

bool McpServer::listTools(const RequestId &id, const llvm::json::Object *params)
{
	if (params && params->get("cursor"))
		return error(&id, INVALID_PARAMS, "Tool list cursor is not supported");

	return reply(id, llvm::json::Object{ { "tools", toolDefinitions(tools.canReindex()) } });
}

bool McpServer::callTool(const RequestId &id, const llvm::json::Object *params, const std::string &payload)
{
	if (!params || !params->getString("name"))
		return error(&id, INVALID_PARAMS, "Invalid tool call");

	auto control = std::make_shared<McpRequestControl>();

	control->deadline = std::chrono::steady_clock::now() + request_timeout;

	{
		std::lock_guard<std::mutex> guard(requests_mutex);

		if (!requests.emplace(id.key(), control).second)
			return error(&id, INVALID_REQUEST, "Duplicate request ID");
	}

	std::thread(&McpServer::runTool, this, id, payload, std::move(control)).detach();

	return true;
}

void McpServer::runTool(RequestId id, std::string payload, std::shared_ptr<McpRequestControl> control)
{
	std::thread deadline(&McpRequestControl::waitForDeadline, control.get());
	auto parsed = llvm::json::parse(payload);

	if (!parsed) {
		llvm::consumeError(parsed.takeError());
		if (!error(&id, INTERNAL_ERROR, "Failed to parse queued request"))
			transport_failed = true;
		control->complete();
		deadline.join();
		finish(id);

		return;
	}

	const llvm::json::Object *message = parsed->getAsObject();
	const llvm::json::Object *params = message ? message->getObject("params") : nullptr;
	auto name = params ? params->getString("name") : std::nullopt;
	const llvm::json::Object *arguments = params ? params->getObject("arguments") : nullptr;

	if (!name || (params->get("arguments") && !arguments)) {
		if (!error(&id, INVALID_PARAMS, "Invalid tool call"))
			transport_failed = true;
		control->complete();
		deadline.join();
		finish(id);

		return;
	}

	McpToolResult result = tools.call(*name, arguments, *control);

	control->complete();
	deadline.join();

	if (control->cancelled)
		result = { McpToolStatus::Cancelled, {}, "Request cancelled" };
	else if (control->timed_out)
		result = { McpToolStatus::DeadlineExceeded, {}, "Request deadline exceeded" };

	bool written;

	if (result.status == McpToolStatus::UnknownTool)
		written = error(&id, INVALID_PARAMS, "Unknown tool");
	else if (result.status == McpToolStatus::Cancelled)
		written = error(&id, REQUEST_CANCELLED, result.message);
	else if (result.status == McpToolStatus::DeadlineExceeded)
		written = error(&id, DEADLINE_EXCEEDED, result.message);
	else if (result.status == McpToolStatus::DatabaseError)
		written = error(&id, INTERNAL_ERROR, result.message);
	else
		written = reply(id, toolResult(std::move(result)));

	if (!written)
		transport_failed = true;
	finish(id);
}

void McpServer::cancel(const llvm::json::Object *params)
{
	if (!params)
		return;

	auto id = requestId(params->get("requestId"));

	if (!id)
		return;

	std::lock_guard<std::mutex> guard(requests_mutex);
	auto request = requests.find(id->key());

	if (request != requests.end())
		request->second->cancel();
}

void McpServer::finish(const RequestId &id)
{
	std::lock_guard<std::mutex> guard(requests_mutex);

	requests.erase(id.key());
	requests_changed.notify_all();
}

bool McpServer::dispatch(const llvm::json::Object &message, const std::string &payload)
{
	auto jsonrpc = message.getString("jsonrpc");
	auto method = message.getString("method");
	const llvm::json::Value *raw_id = message.get("id");
	auto id = requestId(raw_id);

	if (!jsonrpc || *jsonrpc != "2.0" || !method || (raw_id && !id))
		return error(nullptr, INVALID_REQUEST, "Invalid Request");

	if (*method == "notifications/cancelled") {
		if (id)
			return error(&*id, INVALID_REQUEST, "Invalid Request");

		cancel(message.getObject("params"));

		return true;
	}

	if (*method == "initialize")
		return id ? initialize(*id, message.getObject("params"))
			  : error(nullptr, INVALID_REQUEST, "Invalid Request");

	if (*method == "ping")
		return id ? reply(*id, llvm::json::Object{}) : true;

	if (state == State::Uninitialized)
		return id ? error(&*id, SERVER_NOT_INITIALIZED, "Server not initialized") : true;

	if (*method == "notifications/initialized") {
		if (id || state != State::AwaitingInitialized)
			return id ? error(&*id, INVALID_REQUEST, "Invalid Request") : true;

		state = State::Running;

		return true;
	}

	if (state != State::Running)
		return id ? error(&*id, SERVER_NOT_INITIALIZED, "Server not initialized") : true;

	if (*method == "tools/list")
		return id ? listTools(*id, message.getObject("params")) : true;

	if (*method == "tools/call")
		return id ? callTool(*id, message.getObject("params"), payload) : true;

	return id ? error(&*id, METHOD_NOT_FOUND, "Method not found") : true;
}

void McpServer::waitWorkers()
{
	std::unique_lock<std::mutex> lock(requests_mutex);

	requests_changed.wait(lock, [&] { return requests.empty(); });
}

int McpServer::run()
{
	while (!transport_failed) {
		std::string payload;
		auto read = transport.read(payload);

		if (read == McpTransport::ReadResult::EndOfFile)
			break;

		if (read == McpTransport::ReadResult::Error) {
			transport_failed = true;

			break;
		}

		auto parsed = llvm::json::parse(payload);

		if (!parsed) {
			llvm::consumeError(parsed.takeError());

			if (!error(nullptr, PARSE_ERROR, "Parse error"))
				transport_failed = true;

			continue;
		}

		const llvm::json::Object *message = parsed->getAsObject();

		if (!message) {
			if (!error(nullptr, INVALID_REQUEST, "Invalid Request"))
				transport_failed = true;

			continue;
		}

		if (!dispatch(*message, payload))
			transport_failed = true;
	}

	if (transport_failed) {
		std::lock_guard<std::mutex> guard(requests_mutex);

		for (auto &[id, control] : requests)
			control->cancel();
	}

	waitWorkers();

	return transport_failed ? 1 : 0;
}
