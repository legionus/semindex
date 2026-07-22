// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_DATABASE_H
#define SEMINDEX_DATABASE_H

#include "semindex.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct semindex_db semindex_db_t;

typedef enum {
	SEMINDEX_DB_RECORD_ALL,
	SEMINDEX_DB_RECORD_SYMBOL,
	SEMINDEX_DB_RECORD_DECLARATION,
	SEMINDEX_DB_RECORD_DEFINITION,
	SEMINDEX_DB_RECORD_REFERENCE,
} semindex_db_record_filter_t;

typedef enum {
	SEMINDEX_DB_DECLARATION,
	SEMINDEX_DB_DEFINITION,
	SEMINDEX_DB_REFERENCE,
} semindex_db_record_type_t;

typedef struct {
	const char *symbol;
	const char *path;
	const char *variant;
	semindex_db_record_filter_t record;
	unsigned mode;
	semindex_symbol_kind_t kind;
	int has_mode;
	int has_kind;
} semindex_db_query_options_t;

typedef struct {
	const char *variant;
	const char *path;
	const char *symbol;
	const char *context;
	semindex_db_record_type_t record;
	semindex_symbol_kind_t kind;
	unsigned action;
	unsigned mode;
	unsigned line;
	unsigned column;
	unsigned long long usr_id;
	unsigned long long context_usr_id;
	int local;
} semindex_db_record_t;

/* Record strings remain valid only until the callback returns. */
typedef int (*semindex_db_record_callback_t)(void *data, const semindex_db_record_t *record);

int semindex_db_open(const char *path, semindex_db_t **result);
void semindex_db_close(semindex_db_t *db);

/* A nonzero callback result stops iteration and is returned to the caller. */
int semindex_db_query(semindex_db_t *db, const semindex_db_query_options_t *opts,
	semindex_db_record_callback_t callback, void *data);

/* Line and column are one-based source byte positions. */
int semindex_db_find_at(semindex_db_t *db, const char *path, const char *variant, unsigned line, unsigned column,
	semindex_db_record_callback_t callback, void *data);

#ifdef __cplusplus
}
#endif

#endif /* SEMINDEX_DATABASE_H */
