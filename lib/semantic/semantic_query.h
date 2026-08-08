// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "semindex_database.h"
#include "source_resolver.h"

#include <string>
#include <vector>

struct SemindexQueryRecord {
	std::string variant;
	std::string path;
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

	semindex_db_record_t view() const;
};

SemindexQueryRecord semindexQueryRecord(const semindex_db_record_t &record);

class SemindexQueryOverlay
{
public:
	virtual ~SemindexQueryOverlay() = default;

	virtual bool contains(const std::string &path) const = 0;
	virtual int findAt(const std::string &path, const char *variant, unsigned line, unsigned column,
		semindex_db_record_callback_t callback, void *data) const = 0;
};

struct SemindexPositionQuery {
	std::string path;
	std::string variant;
	unsigned line;
	unsigned column;
	const SemindexQueryOverlay *overlay = nullptr;
};

class SemindexQueryService
{
public:
	SemindexQueryService(semindex_db_t *database, const SemindexSourceResolver &sources);

	int recordsAt(const SemindexPositionQuery &query, std::vector<SemindexQueryRecord> &records) const;

private:
	semindex_db_t *database;
	const SemindexSourceResolver &sources;
};
