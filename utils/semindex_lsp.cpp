// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_server.h"
#include "lsp_transport.h"
#include "semindex_database.h"

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
		     "      --variant=NAME         query only the named index variant\n"
		     "  -h, --help                 display this help and exit\n"
		     "\n";
}

int main(int argc, char **argv)
{
	std::string database_path = ".semindex/semindex.db";
	std::string variant;
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
		std::cerr << "semindex-lsp: unknown option: " << argument << '\n';
		usage(std::cerr);
		return 1;
	}
	if (semindex_db_open(database_path.c_str(), &database) < 0)
		return 1;

	LspTransport transport(std::cin, std::cout, std::cerr);
	LspServer server(transport, database, std::move(variant));
	int ret = server.run();
	semindex_db_close(database);
	return ret;
}
