// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <string.h>

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
	int ret = 1;

	if (argc != 4) {
		fprintf(stderr, "usage: database-api DATABASE SOURCE CALLGRAPH_SOURCE\n");
		return 1;
	}
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
	ret = 0;
	goto out;
unexpected:
	fprintf(stderr, "database API returned unexpected callgraph records\n");
out:
	semindex_db_close(db);
	return ret;
}
