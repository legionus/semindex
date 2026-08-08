// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

extern "C" {
#include "command_db.h"
}

#include <memory>
#include <string>
#include <vector>

struct SemindexCommandResolverOptions {
	std::string commands_database;
	std::string compile_commands;
	std::string repository_root;
	bool allow_fallback = false;
};

class SemindexResolvedCommand
{
public:
	enum class Source {
		Saved,
		CompilationDatabase,
		Fallback,
	};

	~SemindexResolvedCommand();

	Source source() const;
	const semindex_compile_command_t *command() const;
	const std::string &compileCommands() const;
	const std::string &commandsDatabase() const;
	const std::string &directory() const;
	bool commandAvailable() const;

private:
	friend class SemindexCommandResolver;

	struct SavedCommandDeleter {
		void operator()(command_db_command_t *command) const;
	};

	Source command_source = Source::Fallback;
	std::unique_ptr<command_db_command_t, SavedCommandDeleter> saved;
	std::string compile_commands;
	std::string commands_database;
	std::string command_directory;
	std::string command_file;
	std::vector<std::string> arguments;
	std::vector<const char *> argument_views;
	semindex_compile_command_t fallback = {};
	bool available = false;
};

struct SemindexCommandResolution {
	std::unique_ptr<SemindexResolvedCommand> command;
	std::string error;
};

class SemindexCommandResolver
{
public:
	explicit SemindexCommandResolver(SemindexCommandResolverOptions options);

	void setCompileCommands(std::string path);
	void setRepositoryRoot(std::string path);
	int commandAvailable(const std::string &file, const std::string &variant) const;
	SemindexCommandResolution resolve(const std::string &file, const std::string &variant) const;

private:
	SemindexCommandResolverOptions options;
};
