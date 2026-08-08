// SPDX-License-Identifier: GPL-2.0-or-later
#include "protocol_logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <utility>

ProtocolLogger::ProtocolLogger(std::ostream &errors, std::ostream *log, std::string component)
    : errors(errors), log(log), component(std::move(component))
{
}

bool ProtocolLogger::write(const char *direction, const std::string &payload)
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
		errors << component << ": failed to write protocol log\n";

		return false;
	}

	return true;
}
