// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_INDEX_DB_H
#define SEMINDEX_INDEX_DB_H

#include "semindex.h"

int index_db_store(const char *path, semindex_t *s, const char *main_file);

#endif /* SEMINDEX_INDEX_DB_H */
