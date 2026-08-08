// SPDX-License-Identifier: GPL-2.0-or-later
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "repository.h"
#include "semindex_paths.h"

char *semindex_repository_root_explicit(const char *input)
{
	struct stat input_stat;
	char *path;

	if (!input || !(path = realpath(input, NULL)))
		return NULL;

	if (stat(path, &input_stat) < 0 || !S_ISDIR(input_stat.st_mode)) {
		free(path);

		return NULL;
	}

	return path;
}

char *semindex_repository_root(const char *input)
{
	struct stat input_stat;
	char *path;
	char *slash;

	if (!input || !(path = realpath(input, NULL)))
		return NULL;

	if (stat(path, &input_stat) < 0) {
		free(path);
		return NULL;
	}

	if (!S_ISDIR(input_stat.st_mode)) {
		slash = strrchr(path, '/');

		if (!slash) {
			free(path);
			return NULL;
		}

		if (slash == path)
			slash[1] = '\0';
		else
			*slash = '\0';
	}

	for (;;) {
		char *marker;
		struct stat marker_stat;

		marker = sqlite3_mprintf("%s%s.git", path, !strcmp(path, "/") ? "" : "/");

		if (!marker) {
			free(path);
			return NULL;
		}

		if (lstat(marker, &marker_stat) == 0) {
			sqlite3_free(marker);
			return path;
		}

		sqlite3_free(marker);

		if (!strcmp(path, "/"))
			break;

		slash = strrchr(path, '/');

		if (slash == path)
			slash[1] = '\0';
		else
			*slash = '\0';
	}

	free(path);
	return NULL;
}

char *semindex_repository_path(const char *root, const char *path)
{
	char *resolved;
	size_t root_len;
	char *result;
	int outside_root;

	if (!root || !path)
		return path ? strdup(path) : NULL;

	resolved = realpath(path, NULL);

	if (!resolved)
		return strdup(path);

	root_len = strlen(root);
	outside_root = strncmp(resolved, root, root_len);

	if (!outside_root && strcmp(root, "/") && resolved[root_len] && resolved[root_len] != '/')
		outside_root = 1;

	if (outside_root)
		return resolved;

	if (!resolved[root_len])
		result = strdup(".");
	else
		result = strdup(resolved + root_len + 1);

	free(resolved);
	return result;
}

char *semindex_default_database_path(const char *path, const char *name)
{
	char *allocated_root;
	const char *root;
	char *result;
	size_t size;

	if (!name || !name[0])
		return NULL;

	allocated_root = semindex_repository_root(path ? path : ".");
	root = allocated_root ? allocated_root : ".";

	size = strlen(root) + strlen(SEMINDEX_STATE_DIRECTORY) + strlen(name) + 3;
	result = malloc(size);

	if (result)
		snprintf(result, size, "%s/" SEMINDEX_STATE_DIRECTORY "/%s", root, name);

	free(allocated_root);

	return result;
}
