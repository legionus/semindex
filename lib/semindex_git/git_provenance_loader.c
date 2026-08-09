// SPDX-License-Identifier: GPL-2.0-or-later
#include <dlfcn.h>
#include <string.h>

#include "git_provenance.h"

typedef int (*provenance_fn_t)(const char *path, semindex_git_provenance_data_t *provenance);
typedef void (*destroy_fn_t)(semindex_git_provenance_data_t *provenance);
typedef int blob_fn_t(const char *repository_root, const char *commit, const char *path,
	semindex_git_blob_data_t *blob);
typedef void (*blob_destroy_fn_t)(semindex_git_blob_data_t *blob);

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

int semindex_git_blob(const char *repository_root, const char *commit, const char *path, semindex_git_blob_t *blob)
{
	blob_fn_t *load;

	memset(blob, 0, sizeof(*blob));
	blob->backend = dlopen(SEMINDEX_GIT_BACKEND_NAME, RTLD_NOW | RTLD_LOCAL);

	if (!blob->backend)
		return 1;

	load = (blob_fn_t *)dlsym(blob->backend, "semindex_git_backend_blob");
	blob->destroy = (blob_destroy_fn_t)dlsym(blob->backend, "semindex_git_backend_blob_destroy");

	if (!load || !blob->destroy)
		goto fail;

	return load(repository_root, commit, path, &blob->data);

fail:
	dlclose(blob->backend);
	memset(blob, 0, sizeof(*blob));

	return -1;
}

void semindex_git_blob_destroy(semindex_git_blob_t *blob)
{
	if (blob->destroy)
		blob->destroy(&blob->data);

	if (blob->backend)
		dlclose(blob->backend);

	memset(blob, 0, sizeof(*blob));
}
