// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "protocol_logger.h"

#include <llvm/Support/JSON.h>

#include <iosfwd>
#include <mutex>
#include <string>

class McpTransport
{
public:
	enum class ReadResult {
		Message,
		EndOfFile,
		Error,
	};

	McpTransport(std::istream &input, std::ostream &output, std::ostream &errors, std::ostream *log = nullptr);

	ReadResult read(std::string &payload);
	bool write(const llvm::json::Value &message);

private:
	std::istream &input;
	std::ostream &output;
	std::ostream &errors;
	ProtocolLogger logger;
	std::mutex write_mutex;
};
