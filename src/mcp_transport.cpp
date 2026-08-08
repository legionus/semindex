// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp_transport.h"

#include <llvm/Support/raw_ostream.h>

#include <istream>
#include <ostream>

static constexpr size_t MAX_MESSAGE_SIZE = 4 * 1024 * 1024;

McpTransport::McpTransport(std::istream &input, std::ostream &output, std::ostream &errors, std::ostream *log)
    : input(input), output(output), errors(errors), logger(errors, log, "semindex-mcp")
{
}

McpTransport::ReadResult McpTransport::read(std::string &payload)
{
	payload.clear();

	if (!std::getline(input, payload))
		return input.eof() ? ReadResult::EndOfFile : ReadResult::Error;

	if (!payload.empty() && payload.back() == '\r')
		payload.pop_back();

	if (payload.empty() || payload.size() > MAX_MESSAGE_SIZE) {
		errors << "semindex-mcp: invalid message size\n";

		return ReadResult::Error;
	}

	{
		std::lock_guard<std::mutex> guard(write_mutex);

		if (!logger.write("CLIENT --> SERVER", payload))
			return ReadResult::Error;
	}

	return ReadResult::Message;
}

bool McpTransport::write(const llvm::json::Value &message)
{
	std::string payload;
	llvm::raw_string_ostream stream(payload);

	stream << message;
	stream.flush();

	std::lock_guard<std::mutex> guard(write_mutex);

	output << payload << '\n';
	output.flush();

	if (!output) {
		errors << "semindex-mcp: failed to write response\n";

		return false;
	}

	return logger.write("SERVER --> CLIENT", payload);
}
