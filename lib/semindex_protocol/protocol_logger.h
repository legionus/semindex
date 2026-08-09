// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <iosfwd>
#include <string>

class ProtocolLogger
{
public:
	ProtocolLogger(std::ostream &errors, std::ostream *log, std::string component);

	bool write(const char *direction, const std::string &payload);

private:
	std::ostream &errors;
	std::ostream *log;
	std::string component;
};
