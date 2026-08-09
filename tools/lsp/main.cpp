// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_indexer.h"
#include "lsp_server.h"
#include "lsp_transport.h"
#include "server_environment.h"
#include "semindex_paths.h"

#include <filesystem>
#include <getopt.h>
#include <iostream>
#include <string>
#include <utility>

static void usage(std::ostream &stream)
{
	stream << "Usage: semindex-lsp [OPTION]...\n";
}

static void help()
{
	usage(std::cout);
	std::cout << "\n"
		     "Serve Language Server Protocol messages over standard input and output.\n"
		     "\n"
		     "Options:\n"
		     "  -d, --database=PATH        path to the semindex database\n"
		     "                             (default: " SEMINDEX_DEFAULT_SYMBOL_DATABASE ")\n"
		     "      --commands-database=PATH\n"
		     "                             path to the compiler command database\n"
		     "                             (default: commands.db beside --database)\n"
		     "      --compile-commands=PATH path to compile_commands.json\n"
		     "      --variant=NAME         query only the named index variant\n"
		     "      --no-include-local     omit local symbols when reindexing\n"
		     "      --logfile=FILE         append JSON-RPC requests and responses to FILE\n"
		     "  -h, --help                 display this help and exit\n"
		     "\n";
}

int main(int argc, char **argv)
{
	enum {
		OPT_COMMANDS_DATABASE = 1,
		OPT_COMPILE_COMMANDS,
		OPT_VARIANT,
		OPT_NO_INCLUDE_LOCAL,
		OPT_LOGFILE,
	};
	static const struct option long_options[] = {
		{ "database", required_argument, nullptr, 'd' },
		{ "commands-database", required_argument, nullptr, OPT_COMMANDS_DATABASE },
		{ "compile-commands", required_argument, nullptr, OPT_COMPILE_COMMANDS },
		{ "variant", required_argument, nullptr, OPT_VARIANT },
		{ "no-include-local", no_argument, nullptr, OPT_NO_INCLUDE_LOCAL },
		{ "logfile", required_argument, nullptr, OPT_LOGFILE },
		{ "help", no_argument, nullptr, 'h' },
		{ nullptr, 0, nullptr, 0 },
	};
	std::string database_path;
	std::string commands_database_path;
	std::string compile_commands_path;
	std::string variant;
	std::string logfile_path;
	bool database_explicit = false;
	bool include_local = true;
	bool logfile_requested = false;

	int opt;

	optind = 1;

	while ((opt = getopt_long(argc, argv, "+d:h", long_options, nullptr)) != -1) {
		switch (opt) {
		case 'd':
			database_path = optarg;
			database_explicit = true;
			break;
		case 'h':
			help();

			return 0;
		case OPT_COMMANDS_DATABASE:
			commands_database_path = optarg;
			break;
		case OPT_COMPILE_COMMANDS:
			compile_commands_path = optarg;
			break;
		case OPT_VARIANT:
			variant = optarg;
			break;
		case OPT_NO_INCLUDE_LOCAL:
			include_local = false;
			break;
		case OPT_LOGFILE:
			logfile_path = optarg;
			logfile_requested = true;
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

	if (logfile_requested && logfile_path.empty()) {
		std::cerr << "semindex-lsp: empty log file path\n";
		return 1;
	}

	if (!compile_commands_path.empty())
		compile_commands_path = std::filesystem::absolute(compile_commands_path).lexically_normal().string();

	SemindexServerEnvironment environment;
	std::string error;
	SemindexServerEnvironmentOptions environment_options = {
		.root = ".",
		.database = std::move(database_path),
		.commands_database = std::move(commands_database_path),
		.logfile = std::move(logfile_path),
		.database_explicit = database_explicit,
		.create_database = true,
		.open_database = true,
	};

	if (!environment.initialize(environment_options, error)) {
		std::cerr << "semindex-lsp: " << error << '\n';

		return 1;
	}

	LspTransport transport(std::cin, std::cout, std::cerr, environment.logStream());
	LspIndexer indexer(environment.databasePath(), environment.commandsDatabasePath(), compile_commands_path,
		variant.empty() ? "general" : variant, include_local);
	LspServer server(transport, environment.database(), indexer, std::move(variant));

	return server.run();
}
