// SPDX-License-Identifier: GPL-2.0-or-later
#include "mcp_server.h"
#include "mcp_tools.h"
#include "mcp_transport.h"

extern "C" {
#include "command_db.h"
#include "repository.h"
#include "semindex_paths.h"
}

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <getopt.h>
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
		     "                             (default: " SEMINDEX_DEFAULT_SYMBOL_DATABASE ")\n"
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

int main(int argc, char **argv)
{
	enum {
		OPT_COMMANDS_DATABASE = 1,
		OPT_VARIANT,
		OPT_WORKSPACE,
		OPT_ALLOW_REINDEX,
		OPT_NO_INCLUDE_LOCAL,
		OPT_LOGFILE,
	};
	static const struct option long_options[] = {
		{ "database", required_argument, nullptr, 'd' },
		{ "commands-database", required_argument, nullptr, OPT_COMMANDS_DATABASE },
		{ "variant", required_argument, nullptr, OPT_VARIANT },
		{ "workspace", required_argument, nullptr, OPT_WORKSPACE },
		{ "allow-reindex", no_argument, nullptr, OPT_ALLOW_REINDEX },
		{ "no-include-local", no_argument, nullptr, OPT_NO_INCLUDE_LOCAL },
		{ "logfile", required_argument, nullptr, OPT_LOGFILE },
		{ "help", no_argument, nullptr, 'h' },
		{ nullptr, 0, nullptr, 0 },
	};
	std::string database;
	std::string commands_database;
	std::string workspace = std::filesystem::current_path().string();
	std::string variant;
	std::string logfile_path;
	bool allow_reindex = false;
	bool database_explicit = false;
	bool include_local = true;
	int opt;

	optind = 1;

	while ((opt = getopt_long(argc, argv, "+d:h", long_options, nullptr)) != -1) {
		switch (opt) {
		case 'd':
			database = optarg;
			database_explicit = true;
			break;
		case 'h':
			help();

			return 0;
		case OPT_COMMANDS_DATABASE:
			commands_database = optarg;
			break;
		case OPT_VARIANT:
			variant = optarg;
			break;
		case OPT_WORKSPACE:
			workspace = optarg;
			break;
		case OPT_ALLOW_REINDEX:
			allow_reindex = true;
			break;
		case OPT_NO_INCLUDE_LOCAL:
			include_local = false;
			break;
		case OPT_LOGFILE:
			logfile_path = optarg;
			break;
		default:
			usage(std::cerr);

			return 1;
		}
	}

	if (optind != argc) {
		usage(std::cerr);

		return 1;
	}

	if (workspace.empty() || (database_explicit && database.empty())) {
		std::cerr << "semindex-mcp: database and workspace paths must not be empty\n";

		return 1;
	}

	std::error_code error;
	std::filesystem::path workspace_path = std::filesystem::canonical(workspace, error);

	if (error || !std::filesystem::is_directory(workspace_path)) {
		std::cerr << "semindex-mcp: invalid workspace: " << workspace << '\n';

		return 1;
	}

	if (!database_explicit) {
		char *path = semindex_default_database_path(workspace_path.c_str(), SEMINDEX_SYMBOL_DATABASE);

		if (!path) {
			std::cerr << "semindex-mcp: failed to allocate database path\n";

			return 1;
		}

		database = path;
		free(path);
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
