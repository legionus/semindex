// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <llvm/Support/JSON.h>

#include <iosfwd>
#include <string>

class LspTransport
{
public:
	enum class ReadResult {
		Message,
		EndOfFile,
		Error,
	};

	LspTransport(std::istream &input, std::ostream &output, std::ostream &errors, std::ostream *log = nullptr);

	ReadResult read(std::string &payload);
	bool write(const llvm::json::Value &message);

private:
	std::istream &input;
	std::ostream &output;
	std::ostream &errors;
	std::ostream *log;

	bool logMessage(const char *direction, const std::string &payload);
};
