// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *f)
{
	fprintf(f, "Usage: semindex COMMAND [OPTION]...\n");
}

static void help(void)
{
	usage(stdout);
	printf("\n"
	       "Index C source files using clang semantic information.\n"
	       "\n"
	       "Commands:\n"
	       "  callgraph                  query direct caller and callee relationships\n"
	       "  cc                         index through a transparent compiler wrapper\n"
	       "  compiler                   index from an explicit compiler argument vector\n"
	       "  compile-commands           export stored compiler commands as JSON\n"
	       "  index                      index a source file using `compile_commands.json'\n"
	       "  lsp                        serve Language Server Protocol requests\n"
	       "  mcp                        serve read-only Model Context Protocol tools\n"
	       "  search                     search stored symbol and use records\n"
	       "\n"
	       "Run 'semindex COMMAND --help' for command-specific help.\n"
	       "\n"
	       "Options:\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "Additional commands can be provided as semindex-COMMAND executables.\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

static int valid_command_char(unsigned char value)
{
	if (value >= 'a' && value <= 'z')
		return 1;

	if (value >= 'A' && value <= 'Z')
		return 1;

	if (value >= '0' && value <= '9')
		return 1;

	return value == '_' || value == '-';
}

static int valid_command(const char *command)
{
	const unsigned char *p = (const unsigned char *)command;

	if (!*p)
		return 0;

	for (; *p; p++)
		if (!valid_command_char(*p))
			return 0;

	return 1;
}

static char *helper_name(const char *command)
{
	size_t length = strlen(command) + sizeof("semindex-");
	char *helper = malloc(length);

	if (!helper)
		return NULL;

	snprintf(helper, length, "semindex-%s", command);
	return helper;
}

static char *sibling_path(const char *launcher, const char *helper)
{
	const char *slash = strrchr(launcher, '/');
	size_t directory_length;
	size_t length;
	char *path;

	if (!slash)
		return NULL;

	directory_length = slash - launcher + 1;
	length = directory_length + strlen(helper) + 1;
	path = malloc(length);

	if (!path)
		return NULL;

	memcpy(path, launcher, directory_length);
	memcpy(path + directory_length, helper, strlen(helper) + 1);
	return path;
}

int main(int argc, char **argv)
{
	char *helper;
	char *sibling;
	const char *command;
	int error;

	if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		help();
		return 0;
	}

	if (argc < 2) {
		usage(stderr);
		return 1;
	}

	command = argv[1];

	if (!valid_command(command)) {
		fprintf(stderr, "semindex: invalid command name: %s\n", command);
		return 1;
	}

	helper = helper_name(command);

	if (!helper) {
		fprintf(stderr, "semindex: failed to allocate command path\n");
		return 1;
	}

	sibling = sibling_path(argv[0], helper);

	if (sibling) {
		execv(sibling, argv + 1);

		if (errno != ENOENT && errno != ENOTDIR) {
			error = errno;
			fprintf(stderr, "semindex: failed to execute %s: %s\n", sibling, strerror(error));
			free(sibling);
			free(helper);
			return 1;
		}
	}

	execvp(helper, argv + 1);
	error = errno;
	fprintf(stderr, "semindex: failed to execute %s: %s\n", helper, strerror(error));
	free(sibling);
	free(helper);
	return error == ENOENT ? 127 : 1;
}
