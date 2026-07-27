// SPDX-License-Identifier: GPL-2.0-or-later
#include <git2.h>
#include <stdlib.h>
#include <string.h>

#include "git_provenance.h"

static char *source_directory(const char *path)
{
	char *directory;
	char *slash;

	directory = realpath(path, NULL);

	if (!directory)
		return NULL;

	slash = strrchr(directory, '/');

	if (!slash) {
		free(directory);

		return NULL;
	}

	if (slash == directory)
		slash[1] = '\0';
	else
		*slash = '\0';

	return directory;
}

static char *repository_root(git_repository *repository)
{
	const char *workdir;
	char *root;
	size_t length;

	workdir = git_repository_workdir(repository);

	if (!workdir)
		return NULL;

	root = realpath(workdir, NULL);

	if (!root)
		return NULL;

	length = strlen(root);

	if (length > 1 && root[length - 1] == '/')
		root[length - 1] = '\0';

	return root;
}

static char *repository_commit(git_repository *repository)
{
	git_object *object = NULL;
	git_reference *head = NULL;
	char commit[GIT_OID_MAX_HEXSIZE + 1];
	const git_oid *oid;
	char *result = NULL;

	if (git_repository_head(&head, repository) < 0)
		goto out;

	if (git_reference_peel(&object, head, GIT_OBJECT_COMMIT) < 0)
		goto out;

	oid = git_object_id(object);

	if (!git_oid_tostr(commit, sizeof(commit), oid))
		goto out;

	result = strdup(commit);
out:
	git_object_free(object);
	git_reference_free(head);

	return result;
}

int semindex_git_backend_provenance(const char *path, semindex_git_provenance_data_t *provenance)
{
	git_repository *repository = NULL;
	char *directory;
	int ret = -1;

	memset(provenance, 0, sizeof(*provenance));
	directory = source_directory(path);

	if (!directory)
		return -1;

	git_libgit2_init();

	if (git_repository_open_ext(&repository, directory, 0, NULL) < 0) {
		ret = 0;
		goto out;
	}

	provenance->repository_root = repository_root(repository);

	provenance->commit = repository_commit(repository);

	ret = 0;
out:
	git_repository_free(repository);
	git_libgit2_shutdown();
	free(directory);

	return ret;
}

void semindex_git_backend_provenance_destroy(semindex_git_provenance_data_t *provenance)
{
	free(provenance->repository_root);
	free(provenance->commit);
	memset(provenance, 0, sizeof(*provenance));
}
