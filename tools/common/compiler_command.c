// SPDX-License-Identifier: GPL-2.0-or-later
#include <stddef.h>
#include <string.h>

#include "compiler_command.h"

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

static int is_source(const char *arg)
{
	return has_suffix(arg, ".c") || has_suffix(arg, ".S");
}

int compiler_command_driver_is_omitted(const char *arg)
{
	return !arg[0] || arg[0] == '-' || arg[0] == '@' || is_source(arg);
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

int compiler_command_find_source(int argc, char **argv, const char **source_file)
{
	int unsupported = 0;
	int sources = 0;
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "-c"))
			continue;

		if (!strcmp(arg, "-E") || !strcmp(arg, "-S") || !strcmp(arg, "-M") || !strcmp(arg, "-MM")) {
			unsupported = 1;
			continue;
		}

		if (!strcmp(arg, "--"))
			continue;

		if (arg[0] == '-') {
			if (option_takes_joined_or_next_arg(arg) && !arg[2])
				i++;

			continue;
		}

		if (!is_source(arg))
			continue;

		*source_file = arg;
		sources++;
	}

	if (unsupported || sources != 1)
		return -1;

	return 0;
}
