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
