// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>

#include "git_provenance.h"

int semindex_git_provenance(const char *path, semindex_git_provenance_t *provenance)
{
	(void)path;
	memset(provenance, 0, sizeof(*provenance));

	return -1;
}

void semindex_git_provenance_destroy(semindex_git_provenance_t *provenance)
{
	memset(provenance, 0, sizeof(*provenance));
}

int semindex_git_blob(const char *repository_root, const char *commit, const char *path, semindex_git_blob_t *blob)
{
	(void)repository_root;
	(void)commit;
	(void)path;
	memset(blob, 0, sizeof(*blob));

	return 1;
}

void semindex_git_blob_destroy(semindex_git_blob_t *blob)
{
	memset(blob, 0, sizeof(*blob));
}
