// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp_server.h"
#include "mcp_tools.h"
#include "mcp_transport.h"

extern "C" {
#include "command_db.h"
}

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

static void usage(std::ostream &stream)
{
	stream << "Usage: semindex-mcp [OPTION]...\n";
}

static void help()
{
	usage(std::cout);
	std::cout << "\n"
		     "Serve bounded Model Context Protocol tools over standard input and output.\n"
		     "\n"
		     "Options:\n"
		     "  -d, --database=PATH        path to the semindex database\n"
		     "                             (default: .semindex/semindex.db)\n"
		     "      --commands-database=PATH\n"
		     "                             path to the compiler command database\n"
		     "                             (default: commands.db beside --database)\n"
		     "      --variant=NAME         query only the named index variant\n"
		     "      --workspace=PATH       restrict source access to PATH\n"
		     "                             (default: current directory)\n"
		     "      --allow-reindex        allow reindex_file to update one source file\n"
		     "      --no-include-local     omit local symbols when reindexing\n"
		     "      --logfile=FILE         append JSON-RPC requests and responses to FILE\n"
		     "  -h, --help                 display this help and exit\n"
		     "\n"
		     "The server uses newline-delimited JSON-RPC on standard input and output.\n"
		     "\n";
}

static bool optionValue(int &index, int argc, char **argv, const std::string &argument, const char *name,
	std::string &value)
{
	std::string prefix = std::string(name) + '=';

	if (argument == name) {
		if (++index == argc)
			return false;

		value = argv[index];

		return true;
	}

	if (argument.rfind(prefix, 0) != 0)
		return false;

	value = argument.substr(prefix.size());

	return true;
}

int main(int argc, char **argv)
{
	std::string database = ".semindex/semindex.db";
	std::string commands_database;
	std::string workspace = std::filesystem::current_path().string();
	std::string variant;
	std::string logfile_path;
	bool allow_reindex = false;
	bool include_local = true;

	for (int i = 1; i < argc; i++) {
		std::string argument(argv[i]);

		if (argument == "-h" || argument == "--help") {
			help();

			return 0;
		}

		if (argument == "-d") {
			if (++i == argc) {
				std::cerr << "semindex-mcp: option requires an argument: " << argument << '\n';

				return 1;
			}

			database = argv[i];
			continue;
		}

		if (argument == "--allow-reindex") {
			allow_reindex = true;

			continue;
		}

		if (argument == "--no-include-local") {
			include_local = false;

			continue;
		}

		if (optionValue(i, argc, argv, argument, "--database", database) ||
			optionValue(i, argc, argv, argument, "--commands-database", commands_database) ||
			optionValue(i, argc, argv, argument, "--variant", variant) ||
			optionValue(i, argc, argv, argument, "--workspace", workspace) ||
			optionValue(i, argc, argv, argument, "--logfile", logfile_path))
			continue;

		std::cerr << "semindex-mcp: unknown option: " << argument << '\n';
		usage(std::cerr);

		return 1;
	}

	if (database.empty() || workspace.empty()) {
		std::cerr << "semindex-mcp: database and workspace paths must not be empty\n";

		return 1;
	}

	if (commands_database.empty()) {
		char *path = command_db_default_path(database.c_str());

		if (!path) {
			std::cerr << "semindex-mcp: failed to allocate command database path\n";

			return 1;
		}

		commands_database = path;
		free(path);
	}

	std::error_code error;
	std::filesystem::path workspace_path = std::filesystem::canonical(workspace, error);

	if (error || !std::filesystem::is_directory(workspace_path)) {
		std::cerr << "semindex-mcp: invalid workspace: " << workspace << '\n';

		return 1;
	}

	database = std::filesystem::absolute(database).lexically_normal().string();
	commands_database = std::filesystem::absolute(commands_database).lexically_normal().string();
	std::ofstream logfile;

	if (!logfile_path.empty()) {
		logfile.open(logfile_path, std::ios::app);

		if (!logfile) {
			std::cerr << "semindex-mcp: failed to open log file: " << logfile_path << '\n';

			return 1;
		}
	}

	McpTransport transport(std::cin, std::cout, std::cerr, logfile.is_open() ? &logfile : nullptr);
	McpToolService tools(McpToolOptions{
		.database = std::move(database),
		.commands_database = std::move(commands_database),
		.workspace = std::move(workspace_path),
		.variant = std::move(variant),
		.allow_reindex = allow_reindex,
		.include_local = include_local,
	});
	McpServer server(transport, tools, std::chrono::seconds(30));

	return server.run();
}
