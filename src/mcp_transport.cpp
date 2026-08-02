// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp_transport.h"

#include <llvm/Support/raw_ostream.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <istream>
#include <ostream>

static constexpr size_t MAX_MESSAGE_SIZE = 4 * 1024 * 1024;

McpTransport::McpTransport(std::istream &input, std::ostream &output, std::ostream &errors, std::ostream *log)
    : input(input), output(output), errors(errors), log(log)
{
}

bool McpTransport::logMessage(const char *direction, const std::string &payload)
{
	if (!log)
		return true;

	auto now = std::chrono::system_clock::now();
	auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now - seconds).count();
	std::time_t time = std::chrono::system_clock::to_time_t(now);
	std::tm utc = {};

	gmtime_r(&time, &utc);
	*log << std::put_time(&utc, "%FT%T") << '.' << std::setfill('0') << std::setw(6) << micros << "Z " << direction
	     << '\n'
	     << payload << "\n\n";
	log->flush();

	if (!*log) {
		errors << "semindex-mcp: failed to write protocol log\n";

		return false;
	}

	return true;
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

		if (!logMessage("CLIENT --> SERVER", payload))
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

	return logMessage("SERVER --> CLIENT", payload);
}
