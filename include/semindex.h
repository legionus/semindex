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
	SEMINDEX_SYMBOL_ENUM,
	SEMINDEX_SYMBOL_ENUM_CONSTANT,
	SEMINDEX_SYMBOL_TYPEDEF,
	SEMINDEX_SYMBOL_FUNCTION,
	SEMINDEX_SYMBOL_MACRO,
	SEMINDEX_SYMBOL_FILE,
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

typedef enum {
	SEMINDEX_SCOPE_FILE,
	SEMINDEX_SCOPE_PROJECT,
	SEMINDEX_SCOPE_ALL,
} semindex_scope_t;

typedef struct {
	const char *directory;
	const char *file;
	size_t argc;
	const char *const *argv;
} semindex_compile_command_t;

/* symbol record */
typedef struct {
	semindex_symbol_kind_t kind;
	const char *name;    /* may be NULL or "" for anonymous */
	const char *owner;   /* containing record for fields */
	const char *type;    /* textual type */
	const char *usr;     /* stable unique id */
	const char *context; /* containing function for local symbols */
	const char *file;
	unsigned line;
	unsigned column;
	int local;
	int definition;
	unsigned long long order;
} semindex_symbol_t;

/* usage record */
typedef struct {
	semindex_use_kind_t kind;
	semindex_symbol_kind_t symbol_kind;
	unsigned mode;			   /* semindex_use_mode_t bitmask */
	const char *name;		   /* target symbol */
	const char *owner;		   /* containing record for fields */
	const char *type;		   /* textual type */
	const char *usr;		   /* target symbol */
	const char *context;		   /* containing function */
	const char *context_usr;	   /* containing function USR for call records */
	unsigned long long usr_id;	   /* hashed target USR for direct calls */
	unsigned long long context_usr_id; /* hashed containing function USR */
	const char *file;
	unsigned line;
	unsigned column;
	int local;
	unsigned long long order;
} semindex_use_t;

/* lifecycle */
semindex_t *semindex_create(void);
void semindex_destroy(semindex_t *s);
void semindex_set_scope(semindex_t *s, semindex_scope_t scope);
void semindex_set_details(semindex_t *s, int enabled);
void semindex_set_include_local(semindex_t *s, int enabled);

/* indexing */
int semindex_index_command(semindex_t *s, const semindex_compile_command_t *cmd);
int semindex_index_file(semindex_t *s, const char *compile_commands_json, const char *source_file);
/* returned pointers are valid until the next index operation or destroy */
const semindex_compile_command_t *semindex_get_compile_command(const semindex_t *s);

/* queries */
size_t semindex_symbol_count(const semindex_t *s);
/* returned pointer is valid until the next index operation or destroy */
const semindex_symbol_t *semindex_get_symbol(const semindex_t *s, size_t idx);

size_t semindex_use_count(const semindex_t *s);
/* returned pointer is valid until the next index operation or destroy */
const semindex_use_t *semindex_get_use(const semindex_t *s, size_t idx);

#ifdef __cplusplus
}
#endif

#endif /* SEMINDEX_H */
