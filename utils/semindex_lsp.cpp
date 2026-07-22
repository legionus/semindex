// SPDX-License-Identifier: GPL-2.0-or-later
#include "lsp_server.h"
#include "lsp_transport.h"

#include <iostream>
#include <string>

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
		     "  -h, --help                 display this help and exit\n"
		     "\n";
}

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		std::string argument(argv[i]);

		if (argument == "-h" || argument == "--help") {
			help();
			return 0;
		}
		std::cerr << "semindex-lsp: unknown option: " << argument << '\n';
		usage(std::cerr);
		return 1;
	}

	LspTransport transport(std::cin, std::cout, std::cerr);
	LspServer server(transport);
	return server.run();
}
