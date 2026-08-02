// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_GIT_PROVENANCE_H
#define SEMINDEX_GIT_PROVENANCE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	char *repository_root;
	char *commit;
} semindex_git_provenance_data_t;

typedef struct {
	semindex_git_provenance_data_t data;
	void *backend;
	void (*destroy)(semindex_git_provenance_data_t *data);
} semindex_git_provenance_t;

typedef struct {
	const void *content;
	size_t size;
	void *object;
} semindex_git_blob_data_t;

typedef struct {
	semindex_git_blob_data_t data;
	void *backend;
	void (*destroy)(semindex_git_blob_data_t *data);
} semindex_git_blob_t;

int semindex_git_backend_provenance(const char *path, semindex_git_provenance_data_t *provenance);
void semindex_git_backend_provenance_destroy(semindex_git_provenance_data_t *provenance);
int semindex_git_backend_blob(const char *repository_root, const char *commit, const char *path,
	semindex_git_blob_data_t *blob);
void semindex_git_backend_blob_destroy(semindex_git_blob_data_t *blob);

int semindex_git_provenance(const char *path, semindex_git_provenance_t *provenance);
void semindex_git_provenance_destroy(semindex_git_provenance_t *provenance);
int semindex_git_blob(const char *repository_root, const char *commit, const char *path, semindex_git_blob_t *blob);
void semindex_git_blob_destroy(semindex_git_blob_t *blob);

#ifdef __cplusplus
}
#endif

#endif /* SEMINDEX_GIT_PROVENANCE_H */
