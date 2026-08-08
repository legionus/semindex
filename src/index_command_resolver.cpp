// SPDX-License-Identifier: GPL-2.0-or-later
#include "index_command_resolver.h"

#include <filesystem>
#include <system_error>
#include <utility>

void SemindexResolvedCommand::SavedCommandDeleter::operator()(command_db_command_t *command) const
{
	command_db_command_free(command);
}

SemindexResolvedCommand::~SemindexResolvedCommand() = default;

SemindexResolvedCommand::Source SemindexResolvedCommand::source() const
{
	return command_source;
}

const semindex_compile_command_t *SemindexResolvedCommand::command() const
{
	if (saved)
		return command_db_command_get(saved.get());

	if (command_source == Source::Fallback)
		return &fallback;

	return nullptr;
}

const std::string &SemindexResolvedCommand::compileCommands() const
{
	return compile_commands;
}

const std::string &SemindexResolvedCommand::commandsDatabase() const
{
	return commands_database;
}

const std::string &SemindexResolvedCommand::directory() const
{
	return command_directory;
}

bool SemindexResolvedCommand::commandAvailable() const
{
	return available;
}

SemindexCommandResolver::SemindexCommandResolver(SemindexCommandResolverOptions options) : options(std::move(options))
{
}

void SemindexCommandResolver::setCompileCommands(std::string path)
{
	options.compile_commands = std::move(path);
}

void SemindexCommandResolver::setRepositoryRoot(std::string path)
{
	options.repository_root = std::move(path);
}

int SemindexCommandResolver::commandAvailable(const std::string &file, const std::string &variant) const
{
	command_db_command_t *command = nullptr;
	std::error_code error;

	if (!std::filesystem::exists(options.commands_database, error))
		return error ? -1 : 1;

	int ret = command_db_load(options.commands_database.c_str(), variant.c_str(), file.c_str(), &command);

	command_db_command_free(command);

	return ret;
}

SemindexCommandResolution SemindexCommandResolver::resolve(const std::string &file, const std::string &variant) const
{
	auto resolved = std::make_unique<SemindexResolvedCommand>();
	command_db_command_t *saved = nullptr;
	std::error_code error;
	int loaded;

	if (std::filesystem::exists(options.commands_database, error))
		loaded = command_db_load(options.commands_database.c_str(), variant.c_str(), file.c_str(), &saved);
	else
		loaded = error ? -1 : 1;

	if (loaded < 0)
		return { nullptr, "failed to load compiler command for '" + file + "'" };

	if (!loaded) {
		resolved->command_source = SemindexResolvedCommand::Source::Saved;
		resolved->saved.reset(saved);
		resolved->available = true;
		resolved->command_directory = resolved->command()->directory;

		return { std::move(resolved), {} };
	}

	if (!options.compile_commands.empty()) {
		resolved->command_source = SemindexResolvedCommand::Source::CompilationDatabase;
		resolved->compile_commands = options.compile_commands;
		resolved->commands_database = options.commands_database;
		resolved->command_directory = options.repository_root;

		return { std::move(resolved), {} };
	}

	if (!options.allow_fallback)
		return { nullptr, "no saved compiler command for '" + file + "' in variant '" + variant + "'" };

	resolved->command_source = SemindexResolvedCommand::Source::Fallback;
	resolved->command_directory = options.repository_root.empty()
		? std::filesystem::path(file).parent_path().string()
		: options.repository_root;
	resolved->command_file = file;
	resolved->arguments = { "cc", "--no-default-config", "-I", resolved->command_directory };

	std::string include = (std::filesystem::path(resolved->command_directory) / "include").string();

	if (std::filesystem::is_directory(include)) {
		resolved->arguments.push_back("-I");
		resolved->arguments.push_back(std::move(include));
	}

	resolved->arguments.push_back(file);
	resolved->argument_views.reserve(resolved->arguments.size());

	for (const auto &argument : resolved->arguments)
		resolved->argument_views.push_back(argument.c_str());

	resolved->fallback = {
		.directory = resolved->command_directory.c_str(),
		.file = resolved->command_file.c_str(),
		.argc = resolved->argument_views.size(),
		.argv = resolved->argument_views.data(),
	};

	return { std::move(resolved), {} };
}
