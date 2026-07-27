// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_GIT_PROVENANCE_H
#define SEMINDEX_GIT_PROVENANCE_H

typedef struct {
	char *repository_root;
	char *commit;
} semindex_git_provenance_data_t;

typedef struct {
	semindex_git_provenance_data_t data;
	void *backend;
	void (*destroy)(semindex_git_provenance_data_t *data);
} semindex_git_provenance_t;

int semindex_git_backend_provenance(const char *path, semindex_git_provenance_data_t *provenance);
void semindex_git_backend_provenance_destroy(semindex_git_provenance_data_t *provenance);

int semindex_git_provenance(const char *path, semindex_git_provenance_t *provenance);
void semindex_git_provenance_destroy(semindex_git_provenance_t *provenance);

#endif /* SEMINDEX_GIT_PROVENANCE_H */
