// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "mcp_tools.h"
#include "mcp_transport.h"

#include <llvm/Support/JSON.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>

class McpServer
{
public:
	McpServer(McpTransport &transport, McpToolService &tools, std::chrono::milliseconds request_timeout);

	int run();

private:
	enum class State {
		Uninitialized,
		AwaitingInitialized,
		Running,
	};

	struct RequestId {
		bool numeric;
		long long number;
		std::string text;

		std::string key() const;
		llvm::json::Value value() const;
	};

	bool dispatch(const llvm::json::Object &message, const std::string &payload);
	bool initialize(const RequestId &id, const llvm::json::Object *params);
	bool listTools(const RequestId &id, const llvm::json::Object *params);
	bool callTool(const RequestId &id, const llvm::json::Object *params, const std::string &payload);
	void runTool(RequestId id, std::string payload, std::shared_ptr<McpRequestControl> control);
	void cancel(const llvm::json::Object *params);
	void finish(const RequestId &id);
	bool reply(const RequestId &id, llvm::json::Value result);
	bool error(const RequestId *id, int code, llvm::StringRef message);
	void waitWorkers();

	static std::optional<RequestId> requestId(const llvm::json::Value *id);
	static llvm::json::Array toolDefinitions();

	McpTransport &transport;
	McpToolService &tools;
	std::chrono::milliseconds request_timeout;
	State state = State::Uninitialized;
	std::mutex requests_mutex;
	std::condition_variable requests_changed;
	std::map<std::string, std::shared_ptr<McpRequestControl>> requests;
	std::atomic<bool> transport_failed = false;
};
