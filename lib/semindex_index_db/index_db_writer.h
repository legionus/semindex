// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_INDEX_DB_WRITER_H
#define SEMINDEX_INDEX_DB_WRITER_H

#include "perf_trace.h"
#include "semindex.h"

typedef struct {
	const char *repository_root;
	const char *git_commit;
} index_db_provenance_t;

typedef struct {
	const char *path;
	semindex_t *index;
	const char *main_file;
	const char *variant;
	index_db_provenance_t provenance;
	semindex_trace_t *trace;
	int include_local;
} index_db_store_request_t;

int index_db_create(const char *path);
int index_db_store(const index_db_store_request_t *request);

#endif /* SEMINDEX_INDEX_DB_WRITER_H */
