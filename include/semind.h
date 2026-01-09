// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMIND_H
#define SEMIND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* opaque handle */
typedef struct semind semind_t;

/* symbol kind */
typedef enum {
	SEMIND_SYMBOL_VAR,
	SEMIND_SYMBOL_FIELD,
	SEMIND_SYMBOL_STRUCT,
	SEMIND_SYMBOL_UNION,
	SEMIND_SYMBOL_TYPEDEF,
	SEMIND_SYMBOL_FUNCTION,
} semind_symbol_kind_t;

typedef enum {
	SEMIND_USE_READ,
	SEMIND_USE_WRITE,
	SEMIND_USE_ADDR,
	SEMIND_USE_CALL,
} semind_use_kind_t;

/* symbol record */
typedef struct {
	semind_symbol_kind_t kind;
	const char* name; /* may be NULL or "" for anonymous */
	const char* type; /* textual type */
	const char* usr;  /* stable unique id */
	const char* file;
	unsigned line;
	unsigned column;
} semind_symbol_t;

/* usage record */
typedef struct {
	semind_use_kind_t kind;
	const char* usr; /* target symbol */
	const char* file;
	unsigned line;
	unsigned column;
} semind_use_t;

/* lifecycle */
semind_t* semind_create(void);
void semind_destroy(semind_t* s);

/* indexing */
int semind_index_file(semind_t* s, const char* compile_commands_json, const char* source_file);

/* queries */
size_t semind_symbol_count(const semind_t* s);
const semind_symbol_t* semind_get_symbol(const semind_t* s, size_t idx);

size_t semind_use_count(const semind_t* s);
const semind_use_t* semind_get_use(const semind_t* s, size_t idx);

#ifdef __cplusplus
}
#endif

#endif /* SEMIND_H */
