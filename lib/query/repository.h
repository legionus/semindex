// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_REPOSITORY_H
#define SEMINDEX_REPOSITORY_H

#ifdef __cplusplus
extern "C" {
#endif

char *semindex_repository_root(const char *path);
char *semindex_repository_root_explicit(const char *path);
char *semindex_repository_path(const char *root, const char *path);
char *semindex_default_database_path(const char *path, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SEMINDEX_REPOSITORY_H */
