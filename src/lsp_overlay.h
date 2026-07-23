// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex.h"
#include "semindex_database.h"

#include <map>
#include <string>
#include <vector>

class LspOverlay
{
public:
	void replace(const std::string &path, const std::string &directory, const semindex_t *index);
	void erase(const std::string &path);
	bool contains(const std::string &path) const;

	int findAt(const std::string &path, const char *variant, unsigned line, unsigned column,
		semindex_db_record_callback_t callback, void *data) const;
	int query(const std::string &path, const char *variant, const semindex_db_query_options_t &options,
		semindex_db_record_callback_t callback, void *data) const;

private:
	struct Record {
		std::string symbol;
		std::string context;
		semindex_db_record_type_t record;
		semindex_symbol_kind_t kind;
		unsigned action;
		unsigned mode;
		unsigned line;
		unsigned column;
		unsigned long long usr_id;
		unsigned long long context_usr_id;
		int local;
	};

	struct Entry {
		std::vector<Record> records;
	};

	static int emitRecord(const Record &stored, const char *path, const char *variant,
		const semindex_db_query_options_t *options, unsigned line, unsigned column,
		semindex_db_record_callback_t callback, void *data);

	std::map<std::string, Entry> indices;
};
