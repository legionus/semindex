// SPDX-License-Identifier: GPL-2.0-or-later
#include "server_environment.h"

extern "C" {
#include "command_db_common.h"
#include "index_db_writer.h"
#include "repository.h"
#include "semindex_paths.h"
}

#include <cstdlib>
#include <filesystem>

SemindexDatabase::~SemindexDatabase()
{
	if (database)
		semindex_db_close(database);
}

bool SemindexDatabase::open(const std::string &path)
{
	semindex_db_t *handle = nullptr;

	if (database)
		return false;

	if (semindex_db_open(path.c_str(), &handle) < 0)
		return false;

	database = handle;

	return true;
}

semindex_db_t *SemindexDatabase::get() const
{
	return database;
}

bool SemindexServerEnvironment::initialize(const SemindexServerEnvironmentOptions &options, std::string &error)
{
	database_path = options.database;
	commands_database_path = options.commands_database;

	if (options.database_explicit && database_path.empty()) {
		error = "database path must not be empty";

		return false;
	}

	if (database_path.empty()) {
		char *path = semindex_default_database_path(options.root.c_str(), SEMINDEX_SYMBOL_DATABASE);

		if (!path) {
			error = "failed to allocate database path";

			return false;
		}

		database_path = path;
		free(path);
	}

	if (commands_database_path.empty()) {
		char *path = command_db_default_path(database_path.c_str());

		if (!path) {
			error = "failed to allocate command database path";

			return false;
		}

		commands_database_path = path;
		free(path);
	}

	database_path = std::filesystem::absolute(database_path).lexically_normal().string();
	commands_database_path = std::filesystem::absolute(commands_database_path).lexically_normal().string();

	if (!options.logfile.empty()) {
		logfile.open(options.logfile, std::ios::app);

		if (!logfile) {
			error = "failed to open log file: " + options.logfile;

			return false;
		}
	}

	if (options.create_database) {
		std::error_code database_error;

		if (!std::filesystem::exists(database_path, database_error)) {
			if (database_error) {
				error = "failed to inspect database: " + database_error.message();

				return false;
			}

			if (index_db_create(database_path.c_str()) < 0) {
				error = "failed to create database";

				return false;
			}
		}
	}

	if (options.open_database && !database_handle.open(database_path)) {
		error = "failed to open database";

		return false;
	}

	return true;
}

const std::string &SemindexServerEnvironment::databasePath() const
{
	return database_path;
}

const std::string &SemindexServerEnvironment::commandsDatabasePath() const
{
	return commands_database_path;
}

std::ofstream *SemindexServerEnvironment::logStream()
{
	return logfile.is_open() ? &logfile : nullptr;
}

semindex_db_t *SemindexServerEnvironment::database() const
{
	return database_handle.get();
}
