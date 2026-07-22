// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_server.h"

#include <llvm/Support/Error.h>

#include <utility>

static constexpr int PARSE_ERROR = -32700;
static constexpr int INVALID_REQUEST = -32600;
static constexpr int METHOD_NOT_FOUND = -32601;
static constexpr int SERVER_NOT_INITIALIZED = -32002;

LspServer::LspServer(LspTransport &transport) : transport(transport)
{
}

bool LspServer::reply(const llvm::json::Value &id, llvm::json::Value result)
{
	return transport.write(llvm::json::Object{
		{ "jsonrpc", "2.0" },
		{ "id", id },
		{ "result", std::move(result) },
	});
}

bool LspServer::error(const llvm::json::Value *id, int code, const char *message)
{
	return transport.write(llvm::json::Object{
		{ "jsonrpc", "2.0" },
		{ "id", id ? *id : llvm::json::Value(nullptr) },
		{ "error",
			llvm::json::Object{
				{ "code", code },
				{ "message", message },
			} },
	});
}

bool LspServer::dispatch(const llvm::json::Object &message)
{
	const llvm::json::Value *id = message.get("id");
	auto jsonrpc = message.getString("jsonrpc");
	auto method = message.getString("method");

	if (!jsonrpc || *jsonrpc != "2.0" || !method)
		return error(id, INVALID_REQUEST, "Invalid Request");

	if (*method == "exit") {
		if (id)
			return error(id, INVALID_REQUEST, "Invalid Request");
		exit_status = state == State::Shutdown ? 0 : 1;
		exiting = true;
		return true;
	}

	if (*method == "initialize") {
		if (!id || state != State::Uninitialized)
			return error(id, INVALID_REQUEST, "Invalid Request");
		state = State::Running;
		return reply(*id,
			llvm::json::Object{
				{ "capabilities",
					llvm::json::Object{
						{ "positionEncoding", "utf-16" },
					} },
				{ "serverInfo",
					llvm::json::Object{
						{ "name", "semindex" },
					} },
			});
	}

	if (state == State::Uninitialized)
		return id ? error(id, SERVER_NOT_INITIALIZED, "Server not initialized") : true;
	if (state == State::Shutdown)
		return id ? error(id, INVALID_REQUEST, "Invalid Request") : true;

	if (*method == "initialized")
		return id ? error(id, INVALID_REQUEST, "Invalid Request") : true;
	if (*method == "shutdown") {
		if (!id)
			return true;
		state = State::Shutdown;
		return reply(*id, nullptr);
	}

	return id ? error(id, METHOD_NOT_FOUND, "Method not found") : true;
}

int LspServer::run()
{
	while (!exiting) {
		std::string payload;
		auto read = transport.read(payload);

		if (read == LspTransport::ReadResult::EndOfFile)
			break;
		if (read == LspTransport::ReadResult::Error)
			return 1;

		auto parsed = llvm::json::parse(payload);
		if (!parsed) {
			llvm::consumeError(parsed.takeError());
			if (!error(nullptr, PARSE_ERROR, "Parse error"))
				return 1;
			continue;
		}
		const llvm::json::Object *message = parsed->getAsObject();
		if (!message) {
			if (!error(nullptr, INVALID_REQUEST, "Invalid Request"))
				return 1;
			continue;
		}
		if (!dispatch(*message))
			return 1;
	}
	return exiting ? exit_status : 0;
}
