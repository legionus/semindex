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

typedef enum {
	SEMINDEX_MODE_R_AOF = 1U << 0,
	SEMINDEX_MODE_W_AOF = 1U << 1,
	SEMINDEX_MODE_R_VAL = 1U << 2,
	SEMINDEX_MODE_W_VAL = 1U << 3,
	SEMINDEX_MODE_R_PTR = 1U << 4,
	SEMINDEX_MODE_W_PTR = 1U << 5,
} semindex_use_mode_t;

/* symbol record */
typedef struct {
	semindex_symbol_kind_t kind;
	const char* name; /* may be NULL or "" for anonymous */
	const char* owner; /* containing record for fields */
	const char* type; /* textual type */
	const char* usr;  /* stable unique id */
	const char* context; /* containing function for local symbols */
	const char* file;
	unsigned line;
	unsigned column;
	int local;
} semindex_symbol_t;

/* usage record */
typedef struct {
	semindex_use_kind_t kind;
	semindex_symbol_kind_t symbol_kind;
	unsigned mode; /* semindex_use_mode_t bitmask */
	const char* name; /* target symbol */
	const char* owner; /* containing record for fields */
	const char* type; /* textual type */
	const char* usr; /* target symbol */
	const char* context; /* containing function */
	const char* file;
	unsigned line;
	unsigned column;
	int local;
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
