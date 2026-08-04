// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_REPOSITORY_H
#define SEMINDEX_REPOSITORY_H

char *semindex_repository_root(const char *path);
char *semindex_repository_root_explicit(const char *path);
char *semindex_repository_path(const char *root, const char *path);

#endif /* SEMINDEX_REPOSITORY_H */
