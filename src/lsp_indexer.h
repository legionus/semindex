// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

class LspIndexer
{
public:
	LspIndexer(std::string database, std::string commands_database, std::string variant, bool include_local);

	bool update(const std::string &file, std::string &error) const;

private:
	std::string database;
	std::string commands_database;
	std::string variant;
	bool include_local;
};
