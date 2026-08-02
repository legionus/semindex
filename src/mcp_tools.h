// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex_database.h"

#include <llvm/Support/JSON.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <string>

struct McpRequestControl {
	std::atomic<bool> cancelled = false;
	std::atomic<bool> timed_out = false;
	std::chrono::steady_clock::time_point deadline;
	mutable std::mutex database_mutex;
	std::mutex completion_mutex;
	std::condition_variable completion_changed;
	semindex_db_t *database = nullptr;
	bool completed = false;

	bool stopped() const;
	void bind(semindex_db_t *database);
	void unbind();
	void cancel();
	void waitForDeadline();
	void complete();
};

enum class McpToolStatus {
	Success,
	InvalidParams,
	UnknownTool,
	DatabaseError,
	Cancelled,
	DeadlineExceeded,
};

struct McpToolResult {
	McpToolStatus status = McpToolStatus::Success;
	llvm::json::Object data;
	std::string message;
};

class McpToolService
{
public:
	McpToolService(std::string database, std::filesystem::path workspace, std::string variant);

	McpToolResult call(llvm::StringRef name, const llvm::json::Object *arguments, McpRequestControl &control) const;

private:
	std::string database;
	std::filesystem::path workspace;
	std::string variant;
};
