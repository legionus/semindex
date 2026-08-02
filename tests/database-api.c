// SPDX-License-Identifier: GPL-2.0-or-later
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "semindex_database.h"

struct result_state {
	const char *symbol;
	semindex_db_record_type_t record;
	unsigned line;
	unsigned count;
	int failed;
};

struct function_state {
	const char *symbol;
	unsigned long long usr_id;
	unsigned count;
};

struct call_state {
	const char *caller;
	const char *callee;
	unsigned count;
	int failed;
};

struct variant_state {
	const char *repository_root;
	unsigned count;
	int failed;
};

struct file_state {
	const char *variant;
	const char *path;
	long long mtime_ns;
	long long size;
	unsigned count;
	int failed;
};

#define PAGINATION_RECORDS 128
#define PAGINATION_KEY_SIZE 1024

struct pagination_state {
	char expected[PAGINATION_RECORDS][PAGINATION_KEY_SIZE];
	char variant[64];
	char path[512];
	char symbol[256];
	semindex_db_cursor_t cursor;
	unsigned expected_count;
	unsigned index;
	unsigned page_count;
	int failed;
};

struct identity_state {
	char variant[64];
	char symbol[256];
	semindex_db_identity_t identity;
	semindex_db_record_type_t record;
	unsigned line;
	unsigned count;
	int failed;
};

static int check_record(void *data, const semindex_db_record_t *record)
{
	struct result_state *state = data;

	if (strcmp(record->variant, "general") || strcmp(record->symbol, state->symbol) ||
		record->record != state->record || (state->line && record->line != state->line)) {
		state->failed = 1;
		return -1;
	}
	state->count++;
	return 0;
}

static int run_query(semindex_db_t *db, const char *symbol, semindex_db_record_filter_t filter,
	semindex_db_record_type_t record, unsigned line, unsigned expected)
{
	semindex_db_query_options_t options = {
		.symbol = symbol,
		.variant = "general",
		.context = "",
		.record = filter,
		.has_local = 1,
		.local = 0,
	};
	struct result_state state = {
		.symbol = symbol,
		.record = record,
		.line = line,
	};

	if (semindex_db_query(db, &options, check_record, &state) || state.failed || state.count != expected)
		return -1;
	return 0;
}

static int collect_function(void *data, const semindex_db_record_t *record)
{
	struct function_state *state = data;

	if (strcmp(record->symbol, state->symbol) || record->kind != SEMINDEX_SYMBOL_FUNCTION || !record->usr_id)
		return -1;
	state->usr_id = record->usr_id;
	state->count++;
	return 0;
}

static int check_call(void *data, const semindex_db_record_t *record)
{
	struct call_state *state = data;

	if (strcmp(record->context, state->caller) || (state->callee && strcmp(record->symbol, state->callee)) ||
		record->action != SEMINDEX_USE_CALL || record->kind != SEMINDEX_SYMBOL_FUNCTION || !record->usr_id ||
		!record->context_usr_id) {
		state->failed = 1;
		return -1;
	}
	state->count++;
	return 0;
}

static int check_variant(void *data, const semindex_db_variant_t *variant)
{
	struct variant_state *state = data;
	const char *expected;

	expected = state->count ? "general" : "debug";

	if (strcmp(variant->name, expected) || !variant->repository_root ||
		strcmp(variant->repository_root, state->repository_root) || variant->git_commit) {
		state->failed = 1;

		return -1;
	}

	state->count++;

	return 0;
}

static int check_file(void *data, const semindex_db_file_t *file)
{
	struct file_state *state = data;

	if (strcmp(file->variant, state->variant) || strcmp(file->path, state->path) ||
		file->mtime_ns != state->mtime_ns || file->size != state->size) {
		state->failed = 1;

		return -1;
	}

	state->count++;

	return 0;
}

static int check_file_metadata(semindex_db_t *db, const char *root, const char *path)
{
	char physical_path[PATH_MAX];
	struct file_state file = {
		.variant = "general",
		.path = path,
	};
	struct file_state missing = { 0 };
	struct stat st;
	int length;

	length = snprintf(physical_path, sizeof(physical_path), "%s/%s", root, path);

	if (length < 0 || (size_t)length >= sizeof(physical_path) || stat(physical_path, &st) < 0)
		return -1;

	file.mtime_ns = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
	file.size = st.st_size;

	if (semindex_db_find_file(db, file.variant, file.path, check_file, &file) < 0 || file.failed || file.count != 1)
		return -1;

	if (semindex_db_find_file(db, "general", "tests/missing.c", check_file, &missing) < 0 || missing.count)
		return -1;

	return 0;
}

static int record_key(char *buffer, size_t size, const semindex_db_record_t *record)
{
	int length;

	length = snprintf(buffer, size, "%s|%s|%010u|%010u|%s|%010d|%010u|%010d|%010u", record->variant, record->path,
		record->line, record->column, record->symbol, record->record, record->action, record->kind,
		record->mode);

	return length < 0 || (size_t)length >= size ? -1 : 0;
}

static int copy_text(char *buffer, size_t size, const char *value)
{
	int length;

	length = snprintf(buffer, size, "%s", value);

	return length < 0 || (size_t)length >= size ? -1 : 0;
}

static int collect_expected(void *data, const semindex_db_record_t *record)
{
	struct pagination_state *state = data;

	if (state->expected_count >= PAGINATION_RECORDS)
		return -1;

	if (record_key(state->expected[state->expected_count], PAGINATION_KEY_SIZE, record) < 0)
		return -1;

	state->expected_count++;

	return 0;
}

static int collect_page(void *data, const semindex_db_record_t *record)
{
	struct pagination_state *state = data;
	char key[PAGINATION_KEY_SIZE];

	if (state->index >= state->expected_count || record_key(key, sizeof(key), record) < 0) {
		state->failed = 1;

		return -1;
	}

	if (strcmp(key, state->expected[state->index])) {
		state->failed = 1;

		return -1;
	}

	if (copy_text(state->variant, sizeof(state->variant), record->variant) < 0 ||
		copy_text(state->path, sizeof(state->path), record->path) < 0 ||
		copy_text(state->symbol, sizeof(state->symbol), record->symbol) < 0) {
		state->failed = 1;

		return -1;
	}

	state->cursor = (semindex_db_cursor_t){
		.variant = state->variant,
		.path = state->path,
		.symbol = state->symbol,
		.record = record->record,
		.kind = record->kind,
		.action = record->action,
		.mode = record->mode,
		.line = record->line,
		.column = record->column,
	};
	state->index++;
	state->page_count++;

	return 0;
}

static int check_pagination(semindex_db_t *db, const char *path)
{
	struct pagination_state state = { 0 };
	semindex_db_query_options_t options = {
		.path = path,
		.variant = "general",
	};

	if (semindex_db_query(db, &options, collect_expected, &state) < 0 || !state.expected_count)
		return -1;

	options.limit = 3;

	do {
		state.page_count = 0;
		options.after = state.index ? &state.cursor : NULL;

		if (semindex_db_query(db, &options, collect_page, &state) < 0 || state.failed)
			return -1;
	} while (state.page_count == options.limit);

	return state.index == state.expected_count ? 0 : -1;
}

static int collect_identity(void *data, const semindex_db_record_t *record)
{
	struct identity_state *state = data;

	if (!record->usr_id || copy_text(state->variant, sizeof(state->variant), record->variant) < 0 ||
		copy_text(state->symbol, sizeof(state->symbol), record->symbol) < 0)
		return -1;

	state->identity = (semindex_db_identity_t){
		.variant = state->variant,
		.symbol = state->symbol,
		.usr_id = record->usr_id,
		.kind = record->kind,
	};
	state->count++;

	return 0;
}

static int check_identity_record(void *data, const semindex_db_record_t *record)
{
	struct identity_state *state = data;

	if (strcmp(record->variant, state->identity.variant) || strcmp(record->symbol, state->identity.symbol) ||
		record->usr_id != state->identity.usr_id || record->kind != state->identity.kind ||
		record->record != state->record || record->line != state->line) {
		state->failed = 1;

		return -1;
	}

	state->count++;

	return 0;
}

static int check_identity_queries(semindex_db_t *db, const char *path)
{
	struct identity_state identity = { 0 };
	semindex_db_identity_query_t query = {
		.identity = &identity.identity,
	};
	struct identity_state definition;
	struct identity_state reference;

	if (semindex_db_find_at(db, path, "general", 14, 3, collect_identity, &identity) < 0 || identity.count != 1)
		return -1;

	definition = (struct identity_state){
		.identity = identity.identity,
		.record = SEMINDEX_DB_DEFINITION,
		.line = 8,
	};
	query.record = SEMINDEX_DB_RECORD_DEFINITION;

	if (semindex_db_query_identity(db, &query, check_identity_record, &definition) < 0 || definition.failed ||
		definition.count != 1)
		return -1;

	reference = (struct identity_state){
		.identity = identity.identity,
		.record = SEMINDEX_DB_REFERENCE,
		.line = 14,
	};
	query.record = SEMINDEX_DB_RECORD_REFERENCE;

	if (semindex_db_query_identity(db, &query, check_identity_record, &reference) < 0 || reference.failed ||
		reference.count != 1)
		return -1;

	return 0;
}

int main(int argc, char **argv)
{
	semindex_db_t *db = NULL;
	struct result_state position = {
		.symbol = "Outer",
		.record = SEMINDEX_DB_DEFINITION,
		.line = 6,
	};
	struct function_state caller = { .symbol = "caller" };
	struct function_state leaf = { .symbol = "leaf" };
	semindex_db_query_options_t function_options = {
		.variant = "general",
		.record = SEMINDEX_DB_RECORD_DEFINITION,
		.kind = SEMINDEX_SYMBOL_FUNCTION,
		.has_kind = 1,
	};
	semindex_db_call_options_t call_options = {
		.variant = "general",
	};
	struct call_state callees = { .caller = "caller" };
	struct call_state callers = { .caller = "caller", .callee = "leaf" };
	struct variant_state variants = { 0 };
	int ret = 1;

	if (argc != 5) {
		fprintf(stderr, "usage: database-api DATABASE SOURCE CALLGRAPH_SOURCE REPOSITORY_ROOT\n");
		return 1;
	}

	variants.repository_root = argv[4];

	if (semindex_db_open(argv[1], &db) < 0)
		goto out;
	if (run_query(db, "Outer.y", SEMINDEX_DB_RECORD_DEFINITION, SEMINDEX_DB_DEFINITION, 8, 1) < 0 ||
		run_query(db, "Outer.y", SEMINDEX_DB_RECORD_REFERENCE, SEMINDEX_DB_REFERENCE, 14, 1) < 0 ||
		semindex_db_find_at(db, argv[2], "general", 6, 10, check_record, &position) < 0 || position.failed ||
		position.count != 1) {
		fprintf(stderr, "database API returned unexpected records\n");
		goto out;
	}
	function_options.symbol = caller.symbol;
	function_options.path = argv[3];
	if (semindex_db_query(db, &function_options, collect_function, &caller) < 0 || caller.count != 1)
		goto unexpected;
	function_options.symbol = leaf.symbol;
	if (semindex_db_query(db, &function_options, collect_function, &leaf) < 0 || leaf.count != 1)
		goto unexpected;

	call_options.function = caller.symbol;
	call_options.usr_id = caller.usr_id;
	call_options.direction = SEMINDEX_DB_CALLEES;
	if (semindex_db_query_calls(db, &call_options, check_call, &callees) < 0 || callees.failed ||
		callees.count != 4)
		goto unexpected;
	call_options.function = leaf.symbol;
	call_options.usr_id = leaf.usr_id;
	call_options.direction = SEMINDEX_DB_CALLERS;
	if (semindex_db_query_calls(db, &call_options, check_call, &callers) < 0 || callers.failed ||
		callers.count != 2)
		goto unexpected;
	if (semindex_db_list_variants(db, check_variant, &variants) < 0 || variants.failed || variants.count != 2)
		goto unexpected;
	if (check_file_metadata(db, argv[4], argv[2]) < 0)
		goto unexpected;
	if (check_pagination(db, argv[3]) < 0)
		goto unexpected;
	if (check_identity_queries(db, argv[2]) < 0)
		goto unexpected;
	ret = 0;
	goto out;
unexpected:
	fprintf(stderr, "database API returned unexpected callgraph records\n");
out:
	semindex_db_close(db);
	return ret;
}
