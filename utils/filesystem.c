// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "filesystem.h"

static int mkdir_one(const char *path)
{
	if (mkdir(path, 0777) == 0 || errno == EEXIST)
		return 0;

	fprintf(stderr, "semindex: failed to create directory '%s': %s\n", path, strerror(errno));

	return -1;
}

int semindex_ensure_parent_directory(const char *path)
{
	const char *slash = strrchr(path, '/');

	char *dir;
	char *p;

	if (!slash || slash == path)
		return 0;

	dir = strdup(path);

	if (!dir)
		return -1;

	dir[slash - path] = '\0';

	for (p = dir + 1; *p; p++) {
		if (*p != '/')
			continue;

		*p = '\0';

		if (mkdir_one(dir) < 0)
			goto fail;

		*p = '/';
	}

	if (mkdir_one(dir) < 0)
		goto fail;

	free(dir);

	return 0;

fail:
	free(dir);

	return -1;
}
