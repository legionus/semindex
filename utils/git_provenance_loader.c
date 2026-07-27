// SPDX-License-Identifier: GPL-2.0-or-later
#include <dlfcn.h>
#include <string.h>

#include "git_provenance.h"

typedef int (*provenance_fn_t)(const char *path, semindex_git_provenance_data_t *provenance);
typedef void (*destroy_fn_t)(semindex_git_provenance_data_t *provenance);

int semindex_git_provenance(const char *path, semindex_git_provenance_t *provenance)
{
	provenance_fn_t load;

	memset(provenance, 0, sizeof(*provenance));
	provenance->backend = dlopen(SEMINDEX_GIT_BACKEND_NAME, RTLD_NOW | RTLD_LOCAL);

	if (!provenance->backend)
		return -1;

	load = (provenance_fn_t)dlsym(provenance->backend, "semindex_git_backend_provenance");
	provenance->destroy = (destroy_fn_t)dlsym(provenance->backend, "semindex_git_backend_provenance_destroy");

	if (!load || !provenance->destroy)
		goto fail;

	return load(path, &provenance->data);

fail:
	dlclose(provenance->backend);
	memset(provenance, 0, sizeof(*provenance));

	return -1;
}

void semindex_git_provenance_destroy(semindex_git_provenance_t *provenance)
{
	if (provenance->destroy)
		provenance->destroy(&provenance->data);

	if (provenance->backend)
		dlclose(provenance->backend);

	memset(provenance, 0, sizeof(*provenance));
}
