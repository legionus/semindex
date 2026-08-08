// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex_database.h"

#include <llvm/Support/JSON.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <string>

class SemindexIndexUpdater;

struct McpToolOptions {
	std::string database;
	std::string commands_database;
	std::filesystem::path workspace;
	std::string variant;
	bool allow_reindex = false;
	bool include_local = true;
};

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
	explicit McpToolService(McpToolOptions options);
	~McpToolService();

	McpToolResult call(llvm::StringRef name, const llvm::json::Object *arguments, McpRequestControl &control) const;
	bool canReindex() const;

private:
	McpToolOptions options;
	std::unique_ptr<SemindexIndexUpdater> updater;
};
