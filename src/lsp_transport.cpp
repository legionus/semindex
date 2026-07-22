// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_transport.h"

#include <llvm/Support/raw_ostream.h>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <istream>
#include <ostream>
#include <string>
#include <system_error>

static constexpr size_t MAX_MESSAGE_SIZE = 64 * 1024 * 1024;

LspTransport::LspTransport(std::istream &input, std::ostream &output, std::ostream &errors, std::ostream *log)
    : input(input), output(output), errors(errors), log(log)
{
}

bool LspTransport::logMessage(const char *direction, const std::string &payload)
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
		errors << "semindex-lsp: failed to write protocol log\n";
		return false;
	}
	return true;
}

LspTransport::ReadResult LspTransport::read(std::string &payload)
{
	std::string line;
	size_t content_length = 0;
	bool have_content_length = false;

	payload.clear();
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			break;

		static constexpr char prefix[] = "Content-Length:";
		if (line.compare(0, sizeof(prefix) - 1, prefix))
			continue;
		if (have_content_length) {
			errors << "semindex-lsp: duplicate Content-Length header\n";
			return ReadResult::Error;
		}

		const char *begin = line.data() + sizeof(prefix) - 1;
		const char *end = line.data() + line.size();
		while (begin != end && (*begin == ' ' || *begin == '\t'))
			begin++;
		auto parsed = std::from_chars(begin, end, content_length);
		if (parsed.ec != std::errc() || parsed.ptr != end || !content_length ||
			content_length > MAX_MESSAGE_SIZE) {
			errors << "semindex-lsp: invalid Content-Length header\n";
			return ReadResult::Error;
		}
		have_content_length = true;
	}

	if (!input && line.empty() && !have_content_length)
		return input.eof() ? ReadResult::EndOfFile : ReadResult::Error;
	if (!have_content_length) {
		errors << "semindex-lsp: missing Content-Length header\n";
		return ReadResult::Error;
	}

	payload.resize(content_length);
	input.read(payload.data(), content_length);
	if (static_cast<size_t>(input.gcount()) != content_length) {
		errors << "semindex-lsp: truncated message body\n";
		return ReadResult::Error;
	}
	if (!logMessage("CLIENT --> SERVER", payload))
		return ReadResult::Error;
	return ReadResult::Message;
}

bool LspTransport::write(const llvm::json::Value &message)
{
	std::string payload;
	llvm::raw_string_ostream stream(payload);

	stream << message;
	stream.flush();
	output << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
	output.flush();
	if (!output) {
		errors << "semindex-lsp: failed to write response\n";
		return false;
	}
	return logMessage("SERVER --> CLIENT", payload);
}
