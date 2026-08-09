// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex_database.h"

#include <fstream>
#include <string>

class SemindexDatabase
{
public:
	SemindexDatabase() = default;
	~SemindexDatabase();

	SemindexDatabase(const SemindexDatabase &) = delete;
	SemindexDatabase &operator=(const SemindexDatabase &) = delete;

	bool open(const std::string &path);
	semindex_db_t *get() const;

private:
	semindex_db_t *database = nullptr;
};

struct SemindexServerEnvironmentOptions {
	std::string root;
	std::string database;
	std::string commands_database;
	std::string logfile;
	bool database_explicit = false;
	bool create_database = false;
	bool open_database = false;
};

class SemindexServerEnvironment
{
public:
	bool initialize(const SemindexServerEnvironmentOptions &options, std::string &error);

	const std::string &databasePath() const;
	const std::string &commandsDatabasePath() const;
	std::ofstream *logStream();
	semindex_db_t *database() const;

private:
	std::string database_path;
	std::string commands_database_path;
	std::ofstream logfile;
	SemindexDatabase database_handle;
};
