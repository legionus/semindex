// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_H
#define SEMINDEX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* opaque handle */
typedef struct semindex semindex_t;

/* symbol kind */
typedef enum {
	SEMINDEX_SYMBOL_VAR,
	SEMINDEX_SYMBOL_FIELD,
	SEMINDEX_SYMBOL_STRUCT,
	SEMINDEX_SYMBOL_UNION,
	SEMINDEX_SYMBOL_TYPEDEF,
	SEMINDEX_SYMBOL_FUNCTION,
} semindex_symbol_kind_t;

typedef enum {
	SEMINDEX_USE_READ,
	SEMINDEX_USE_WRITE,
	SEMINDEX_USE_ADDR,
	SEMINDEX_USE_CALL,
} semindex_use_kind_t;

/* symbol record */
typedef struct {
	semindex_symbol_kind_t kind;
	const char* name; /* may be NULL or "" for anonymous */
	const char* type; /* textual type */
	const char* usr;  /* stable unique id */
	const char* file;
	unsigned line;
	unsigned column;
} semindex_symbol_t;

/* usage record */
typedef struct {
	semindex_use_kind_t kind;
	const char* usr; /* target symbol */
	const char* file;
	unsigned line;
	unsigned column;
} semindex_use_t;

/* lifecycle */
semindex_t* semindex_create(void);
void semindex_destroy(semindex_t* s);

/* indexing */
int semindex_index_file(semindex_t* s, const char* compile_commands_json, const char* source_file);

/* queries */
size_t semindex_symbol_count(const semindex_t* s);
/* returned pointer is valid until the next semindex_index_file() or destroy */
const semindex_symbol_t* semindex_get_symbol(const semindex_t* s, size_t idx);

size_t semindex_use_count(const semindex_t* s);
/* returned pointer is valid until the next semindex_index_file() or destroy */
const semindex_use_t* semindex_get_use(const semindex_t* s, size_t idx);

#ifdef __cplusplus
}
#endif

#endif /* SEMINDEX_H */
