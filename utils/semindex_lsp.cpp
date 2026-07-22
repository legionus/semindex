// SPDX-License-Identifier: GPL-2.0-or-later
#include "command_db.h"
#include "lsp_indexer.h"
#include "lsp_server.h"
#include "lsp_transport.h"
#include "semindex_database.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
		     "                             (default: .semindex/semindex.db)\n"
		     "      --commands-database=PATH\n"
		     "                             path to the compiler command database\n"
		     "                             (default: commands.db beside --database)\n"
		     "      --variant=NAME         query only the named index variant\n"
		     "      --include-local        preserve local symbols when reindexing\n"
		     "      --logfile=FILE         append JSON-RPC requests and responses to FILE\n"
		     "  -h, --help                 display this help and exit\n"
		     "\n";
}

int main(int argc, char **argv)
{
	std::string database_path = ".semindex/semindex.db";
	std::string commands_database_path;
	std::string variant;
	std::string logfile_path;
	bool include_local = false;
	bool logfile_requested = false;
	semindex_db_t *database = nullptr;

	for (int i = 1; i < argc; i++) {
		std::string argument(argv[i]);

		if (argument == "-h" || argument == "--help") {
			help();
			return 0;
		}
		if (argument == "-d" || argument == "--database") {
			if (++i == argc) {
				std::cerr << "semindex-lsp: option requires an argument: " << argument << '\n';
				return 1;
			}
			database_path = argv[i];
			continue;
		}
		if (argument.rfind("--database=", 0) == 0) {
			database_path = argument.substr(sizeof("--database=") - 1);
			continue;
		}
		if (argument == "--commands-database") {
			if (++i == argc) {
				std::cerr << "semindex-lsp: option requires an argument: " << argument << '\n';
				return 1;
			}
			commands_database_path = argv[i];
			continue;
		}
		if (argument.rfind("--commands-database=", 0) == 0) {
			commands_database_path = argument.substr(sizeof("--commands-database=") - 1);
			continue;
		}
		if (argument == "--variant") {
			if (++i == argc) {
				std::cerr << "semindex-lsp: option requires an argument: " << argument << '\n';
				return 1;
			}
			variant = argv[i];
			continue;
		}
		if (argument.rfind("--variant=", 0) == 0) {
			variant = argument.substr(sizeof("--variant=") - 1);
			continue;
		}
		if (argument == "--logfile") {
			if (++i == argc) {
				std::cerr << "semindex-lsp: option requires an argument: " << argument << '\n';
				return 1;
			}
			logfile_path = argv[i];
			logfile_requested = true;
			continue;
		}
		if (argument.rfind("--logfile=", 0) == 0) {
			logfile_path = argument.substr(sizeof("--logfile=") - 1);
			logfile_requested = true;
			continue;
		}
		if (argument == "--include-local") {
			include_local = true;
			continue;
		}
		std::cerr << "semindex-lsp: unknown option: " << argument << '\n';
		usage(std::cerr);
		return 1;
	}
	if (logfile_requested && logfile_path.empty()) {
		std::cerr << "semindex-lsp: empty log file path\n";
		return 1;
	}
	if (commands_database_path.empty()) {
		char *path = command_db_default_path(database_path.c_str());

		if (!path) {
			std::cerr << "semindex-lsp: failed to allocate command database path\n";
			return 1;
		}
		commands_database_path = path;
		free(path);
	}
	database_path = std::filesystem::absolute(database_path).lexically_normal().string();
	commands_database_path = std::filesystem::absolute(commands_database_path).lexically_normal().string();
	std::ofstream logfile;
	if (!logfile_path.empty()) {
		logfile.open(logfile_path, std::ios::app);
		if (!logfile) {
			std::cerr << "semindex-lsp: failed to open log file: " << logfile_path << '\n';
			return 1;
		}
	}
	if (semindex_db_open(database_path.c_str(), &database) < 0)
		return 1;

	LspTransport transport(std::cin, std::cout, std::cerr, logfile.is_open() ? &logfile : nullptr);
	LspIndexer indexer(database_path, commands_database_path, variant.empty() ? "general" : variant, include_local);
	LspServer server(transport, database, indexer, std::move(variant));
	int ret = server.run();
	semindex_db_close(database);
	return ret;
}
