// SPDX-License-Identifier: GPL-2.0-or-later
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_db.h"
#include "index_db.h"
#include "semindex_cli.h"

static void compiler_usage(FILE *f)
{
	fprintf(f, "Usage: semindex compiler [OPTION]... -- [COMPILER] ARG...\n");
}

static void compiler_help(void)
{
	compiler_usage(stdout);
	printf("\n"
	       "Index a C source file from an explicit compiler argument vector.\n"
	       "\n"
	       "Arguments after '--' are treated as compiler arguments. COMPILER is\n"
	       "optional and defaults to `cc'.\n"
	       "Commands with exactly one C source file are indexed.\n"
	       "\n"
	       "Options:\n"
	       "  -f, --format=FORMAT        print index using selected format: default,\n"
	       "                             dissect\n"
	       "  -s, --scope=SCOPE          select indexed source scope: file, project, all\n"
	       "                             (default: project)\n"
	       "  -d, --database=PATH        path to the semindex database\n"
	       "                             (default: .semindex/semindex.db)\n"
	       "      --commands-database=PATH\n"
	       "                             path to the compiler command database\n"
	       "                             (default: commands.db beside --database)\n"
	       "      --variant=NAME          store records in the named variant\n"
	       "                             (default: general)\n"
	       "      --no-store-command      do not store the compiler command\n"
	       "      --include-local         store local symbols and their uses\n"
	       "  -h, --help                 display this help and exit\n"
	       "\n"
	       "Report bugs to authors.\n"
	       "\n");
}

static int has_suffix(const char *str, const char *suffix)
{
	size_t str_len;
	size_t suffix_len;

	if (!str || !suffix)
		return 0;

	str_len = strlen(str);
	suffix_len = strlen(suffix);
	if (str_len < suffix_len)
		return 0;

	return !strcmp(str + str_len - suffix_len, suffix);
}

static int is_c_source(const char *arg)
{
	return has_suffix(arg, ".c");
}

static int compiler_is_omitted(const char *arg)
{
	return !arg[0] || arg[0] == '-' || arg[0] == '@' || is_c_source(arg);
}

static char *commands_database_path(const char *database)
{
	const char *slash = strrchr(database, '/');
	size_t dir_len;
	char *path;

	if (!slash)
		return strdup("commands.db");
	dir_len = slash - database + 1;
	path = malloc(dir_len + sizeof("commands.db"));
	if (!path)
		return NULL;
	memcpy(path, database, dir_len);
	memcpy(path + dir_len, "commands.db", sizeof("commands.db"));
	return path;
}

static int option_takes_joined_or_next_arg(const char *arg)
{
	static const char *opts[] = {
		"-D",
		"-I",
		"-U",
		"-include",
		"-imacros",
		"-isystem",
		"-iquote",
		"-idirafter",
		"-iprefix",
		"-iwithprefix",
		"-iwithprefixbefore",
		"-isysroot",
		"-target",
		"-x",
		"-std",
		"-MF",
		"-MT",
		"-MQ",
		"-o",
	};
	size_t i;

	for (i = 0; i < sizeof(opts) / sizeof(opts[0]); i++) {
		const char *opt = opts[i];
		size_t len = strlen(opt);

		if (!strcmp(arg, opt))
			return 1;
		if (!strncmp(arg, opt, len) && arg[len])
			return 0;
	}

	return 0;
}

static int find_source_file(int argc, char **argv, const char **source_file)
{
	int preprocess_only = 0;
	int assemble_only = 0;
	int sources = 0;
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "-c"))
			continue;
		if (!strcmp(arg, "-E")) {
			preprocess_only = 1;
			continue;
		}
		if (!strcmp(arg, "-S")) {
			assemble_only = 1;
			continue;
		}
		if (!strcmp(arg, "--")) {
			continue;
		}
		if (arg[0] == '-') {
			if (option_takes_joined_or_next_arg(arg) && !arg[2])
				i++;
			continue;
		}
		if (!is_c_source(arg))
			continue;

		*source_file = arg;
		sources++;
	}

	if (preprocess_only || assemble_only)
		return -1;
	if (sources != 1)
		return -1;

	return 0;
}

int cmd_compiler(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "include-local", no_argument, NULL, 1 },
		{ "variant", required_argument, NULL, 2 },
		{ "commands-database", required_argument, NULL, 3 },
		{ "no-store-command", no_argument, NULL, 4 },
		{ "database", required_argument, NULL, 'd' },
		{ "format", required_argument, NULL, 'f' },
		{ "scope", required_argument, NULL, 's' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	enum output_format format = FORMAT_DEFAULT;
	semindex_scope_t scope = SEMINDEX_SCOPE_PROJECT;
	const char *database = ".semindex/semindex.db";
	const char *commands_database = NULL;
	const char *variant = "general";
	const char *source_file = NULL;
	semindex_t *s;
	semindex_compile_command_t cmd;
	int compiler_argc;
	char **compiler_argv;
	char **default_argv = NULL;
	char *default_commands_database = NULL;
	int ret;
	int print_output = 0;
	int include_local = 0;
	int store_command = 1;
	int opt;

	optind = 1;
	while ((opt = getopt_long(argc, argv, "+d:f:s:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 1:
			include_local = 1;
			break;
		case 2:
			variant = optarg;
			break;
		case 3:
			commands_database = optarg;
			break;
		case 4:
			store_command = 0;
			break;
		case 'd':
			database = optarg;
			break;
		case 'f':
			if (parse_format(optarg, &format) < 0) {
				fprintf(stderr, "semindex: unknown format: %s\n", optarg);
				return 1;
			}
			print_output = 1;
			break;
		case 's':
			if (parse_scope(optarg, &scope) < 0) {
				fprintf(stderr, "semindex: unknown scope: %s\n", optarg);
				return 1;
			}
			break;
		case 'h':
			compiler_help();
			return 0;
		default:
			compiler_usage(stderr);
			return 1;
		}
	}

	if (optind < argc && !strcmp(argv[optind], "--"))
		optind++;
	if (optind >= argc) {
		compiler_usage(stderr);
		return 1;
	}
	if (!variant[0]) {
		fprintf(stderr, "semindex: variant name must not be empty\n");
		return 1;
	}

	compiler_argc = argc - optind;
	compiler_argv = argv + optind;
	if (compiler_is_omitted(compiler_argv[0])) {
		default_argv = calloc(compiler_argc + 1, sizeof(*default_argv));
		if (!default_argv) {
			fprintf(stderr, "semindex: failed to allocate compiler arguments\n");
			return 1;
		}
		default_argv[0] = "cc";
		memcpy(default_argv + 1, compiler_argv, compiler_argc * sizeof(*compiler_argv));
		compiler_argv = default_argv;
		compiler_argc++;
	}
	if (find_source_file(compiler_argc, compiler_argv, &source_file) < 0) {
		fprintf(stderr, "semindex: unsupported compiler command\n");
		free(default_argv);
		return 1;
	}
	if (store_command && !commands_database) {
		default_commands_database = commands_database_path(database);
		if (!default_commands_database) {
			fprintf(stderr, "semindex: failed to allocate command database path\n");
			free(default_argv);
			return 1;
		}
		commands_database = default_commands_database;
	}

	s = semindex_create();
	semindex_set_scope(s, scope);
	semindex_set_details(s, print_output);
	semindex_set_include_local(s, print_output || include_local);

	cmd.directory = ".";
	cmd.file = source_file;
	cmd.argc = compiler_argc;
	cmd.argv = (const char *const *)compiler_argv;

	if (semindex_index_command(s, &cmd) != 0) {
		fprintf(stderr, "semindex: failed to index compiler command for '%s'\n", source_file);
		semindex_destroy(s);
		free(default_commands_database);
		free(default_argv);
		return 1;
	}
	if (index_db_store(database, s, source_file, variant, include_local) < 0) {
		semindex_destroy(s);
		free(default_commands_database);
		free(default_argv);
		return 1;
	}
	if (store_command &&
		command_db_store(commands_database, variant, cmd.directory, source_file, compiler_argc,
			(const char *const *)compiler_argv) < 0) {
		semindex_destroy(s);
		free(default_commands_database);
		free(default_argv);
		return 1;
	}

	ret = print_output ? output_index(format, s) : 0;

	semindex_destroy(s);
	free(default_commands_database);
	free(default_argv);
	return ret ? 1 : 0;
}
