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

int main(int argc, char **argv)
{
	semindex_db_t *db = NULL;
	struct result_state position = {
		.symbol = "Outer",
		.record = SEMINDEX_DB_DEFINITION,
		.line = 6,
	};
	int ret = 1;

	if (argc != 3) {
		fprintf(stderr, "usage: database-api DATABASE SOURCE\n");
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
	ret = 0;
out:
	semindex_db_close(db);
	return ret;
}
