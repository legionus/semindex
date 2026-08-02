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
	const char *variant;
	const char *path;
	const char *symbol;
	semindex_db_record_type_t record;
	semindex_symbol_kind_t kind;
	unsigned action;
	unsigned mode;
	unsigned line;
	unsigned column;
} semindex_db_cursor_t;

typedef struct {
	const char *symbol;
	const char *path;
	const char *variant;
	const char *context;
	const semindex_db_cursor_t *after;
	size_t limit;
	semindex_db_record_filter_t record;
	unsigned mode;
	unsigned long long usr_id;
	semindex_symbol_kind_t kind;
	int has_mode;
	int has_usr_id;
	int has_kind;
	int has_local;
	int local;
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

typedef enum {
	SEMINDEX_DB_CALLERS,
	SEMINDEX_DB_CALLEES,
} semindex_db_call_direction_t;

typedef struct {
	const char *function;
	const char *variant;
	unsigned long long usr_id;
	semindex_db_call_direction_t direction;
} semindex_db_call_options_t;

typedef struct {
	const char *variant;
	const char *symbol;
	unsigned long long usr_id;
} semindex_db_function_t;

typedef struct {
	const char *variant;
	const char *symbol;
	unsigned long long usr_id;
	semindex_symbol_kind_t kind;
} semindex_db_identity_t;

typedef struct {
	const semindex_db_identity_t *identity;
	const semindex_db_cursor_t *after;
	size_t limit;
	semindex_db_record_filter_t record;
} semindex_db_identity_query_t;

typedef struct {
	const char *name;
	const char *repository_root;
	const char *git_commit;
} semindex_db_variant_t;

typedef struct {
	const char *variant;
	const char *path;
	long long mtime_ns;
	long long size;
} semindex_db_file_t;

typedef struct {
	const char *variant;
	const char *path;
	const char *symbol;
	const char *declared_type;
	semindex_symbol_kind_t kind;
	unsigned long long usr_id;
} semindex_db_symbol_type_t;

typedef struct {
	const char *declared_type;
	const char *path;
} semindex_db_symbol_type_cursor_t;

typedef struct {
	const semindex_db_identity_t *identity;
	const semindex_db_symbol_type_cursor_t *after;
	size_t limit;
} semindex_db_symbol_type_query_t;

/* Record strings remain valid only until the callback returns. */
typedef int (*semindex_db_record_callback_t)(void *data, const semindex_db_record_t *record);

/* Variant strings remain valid only until the callback returns. */
typedef int (*semindex_db_variant_callback_t)(void *data, const semindex_db_variant_t *variant);

/* File strings remain valid only until the callback returns. */
typedef int (*semindex_db_file_callback_t)(void *data, const semindex_db_file_t *file);

/* Type strings remain valid only until the callback returns. */
typedef int (*semindex_db_symbol_type_callback_t)(void *data, const semindex_db_symbol_type_t *type);

SEMINDEX_API int semindex_db_open(const char *path, semindex_db_t **result);
SEMINDEX_API void semindex_db_close(semindex_db_t *db);
/* May be called from another thread to stop an active database operation. */
SEMINDEX_API void semindex_db_interrupt(semindex_db_t *db);

/*
 * A nonzero callback result stops iteration and is returned to the caller.
 * A zero limit is unlimited. To continue a limited query, copy the last
 * record's cursor fields and pass it as `after`; cursor strings are caller-owned.
 */
SEMINDEX_API int semindex_db_query(semindex_db_t *db, const semindex_db_query_options_t *opts,
	semindex_db_record_callback_t callback, void *data);

/* Line and column are one-based source byte positions. */
SEMINDEX_API int semindex_db_find_at(semindex_db_t *db, const char *path, const char *variant, unsigned line,
	unsigned column, semindex_db_record_callback_t callback, void *data);

SEMINDEX_API int semindex_db_query_calls(semindex_db_t *db, const semindex_db_call_options_t *opts,
	semindex_db_record_callback_t callback, void *data);

SEMINDEX_API int semindex_db_query_functions(semindex_db_t *db, const semindex_db_function_t *functions, size_t count,
	semindex_db_record_callback_t callback, void *data);

SEMINDEX_API int semindex_db_query_identity(semindex_db_t *db, const semindex_db_identity_query_t *query,
	semindex_db_record_callback_t callback, void *data);

/* Variants are returned in name order. Provenance fields may be NULL. */
SEMINDEX_API int semindex_db_list_variants(semindex_db_t *db, semindex_db_variant_callback_t callback, void *data);

SEMINDEX_API int semindex_db_find_file(semindex_db_t *db, const char *variant, const char *path,
	semindex_db_file_callback_t callback, void *data);

SEMINDEX_API int semindex_db_query_symbol_types(semindex_db_t *db, const semindex_db_symbol_type_query_t *query,
	semindex_db_symbol_type_callback_t callback, void *data);

#ifdef __cplusplus
}
#endif

#endif /* SEMINDEX_DATABASE_H */
