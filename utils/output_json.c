// SPDX-License-Identifier: GPL-2.0-or-later
#include "output.h"

static const char *symbol_kind_string(semindex_symbol_kind_t kind)
{
	switch (kind) {
	case SEMINDEX_SYMBOL_VAR:
		return "variable";
	case SEMINDEX_SYMBOL_FIELD:
		return "field";
	case SEMINDEX_SYMBOL_STRUCT:
		return "struct";
	case SEMINDEX_SYMBOL_UNION:
		return "union";
	case SEMINDEX_SYMBOL_ENUM:
		return "enum";
	case SEMINDEX_SYMBOL_ENUM_CONSTANT:
		return "enumerator";
	case SEMINDEX_SYMBOL_TYPEDEF:
		return "typedef";
	case SEMINDEX_SYMBOL_FUNCTION:
		return "function";
	case SEMINDEX_SYMBOL_MACRO:
		return "macro";
	case SEMINDEX_SYMBOL_FILE:
		return "file";
	}

	return "unknown";
}

static const char *use_kind_string(semindex_use_kind_t kind)
{
	switch (kind) {
	case SEMINDEX_USE_READ:
		return "read";
	case SEMINDEX_USE_WRITE:
		return "write";
	case SEMINDEX_USE_ADDR:
		return "address";
	case SEMINDEX_USE_CALL:
		return "call";
	}

	return "unknown";
}

static void print_json_string(FILE *out, const char *value)
{
	const unsigned char *p;

	if (!value) {
		fputs("null", out);
		return;
	}

	fputc('"', out);
	for (p = (const unsigned char *)value; *p; p++) {
		switch (*p) {
		case '"':
			fputs("\\\"", out);
			break;
		case '\\':
			fputs("\\\\", out);
			break;
		case '\b':
			fputs("\\b", out);
			break;
		case '\f':
			fputs("\\f", out);
			break;
		case '\n':
			fputs("\\n", out);
			break;
		case '\r':
			fputs("\\r", out);
			break;
		case '\t':
			fputs("\\t", out);
			break;
		default:
			if (*p < 0x20)
				fprintf(out, "\\u%04x", *p);
			else
				fputc(*p, out);
			break;
		}
	}
	fputc('"', out);
}

static void print_optional_json_string(FILE *out, const char *value)
{
	if (!value || !value[0])
		fputs("null", out);
	else
		print_json_string(out, value);
}

static void print_json_id(FILE *out, unsigned long long id)
{
	if (id)
		fprintf(out, "\"%016llx\"", id);
	else
		fputs("null", out);
}

static void print_json_symbol(FILE *out, const semindex_symbol_t *symbol)
{
	fputs("    {\"kind\":", out);
	print_json_string(out, symbol_kind_string(symbol->kind));
	fputs(",\"name\":", out);
	print_json_string(out, symbol->name);
	fputs(",\"owner\":", out);
	print_optional_json_string(out, symbol->owner);
	fputs(",\"type\":", out);
	print_optional_json_string(out, symbol->type);
	fputs(",\"usr\":", out);
	print_optional_json_string(out, symbol->usr);
	fputs(",\"usr_id\":", out);
	print_json_id(out, symbol->usr_id);
	fputs(",\"context\":", out);
	print_optional_json_string(out, symbol->context);
	fputs(",\"file\":", out);
	print_json_string(out, symbol->file);
	fprintf(out, ",\"line\":%u,\"column\":%u,\"local\":%s,\"definition\":%s}", symbol->line, symbol->column,
		symbol->local ? "true" : "false", symbol->definition ? "true" : "false");
}

static void print_json_use(FILE *out, const semindex_use_t *use)
{
	char mode[4];

	output_mode_string(use->mode, mode);
	fputs("    {\"kind\":", out);
	print_json_string(out, use_kind_string(use->kind));
	fputs(",\"symbol_kind\":", out);
	print_json_string(out, symbol_kind_string(use->symbol_kind));
	fputs(",\"mode\":", out);
	print_json_string(out, mode);
	fprintf(out, ",\"mode_bits\":%u,\"name\":", use->mode);
	print_json_string(out, use->name);
	fputs(",\"owner\":", out);
	print_optional_json_string(out, use->owner);
	fputs(",\"type\":", out);
	print_optional_json_string(out, use->type);
	fputs(",\"usr\":", out);
	print_optional_json_string(out, use->usr);
	fputs(",\"usr_id\":", out);
	print_json_id(out, use->usr_id);
	fputs(",\"context\":", out);
	print_optional_json_string(out, use->context);
	fputs(",\"context_usr\":", out);
	print_optional_json_string(out, use->context_usr);
	fputs(",\"context_usr_id\":", out);
	print_json_id(out, use->context_usr_id);
	fputs(",\"file\":", out);
	print_json_string(out, use->file);
	fprintf(out, ",\"line\":%u,\"column\":%u,\"local\":%s}", use->line, use->column, use->local ? "true" : "false");
}

int output_json(FILE *out, semindex_t *s)
{
	size_t count;

	fputs("{\n  \"version\": 1,\n  \"symbols\": [\n", out);
	count = semindex_symbol_count(s);
	for (size_t i = 0; i < count; i++) {
		print_json_symbol(out, semindex_get_symbol(s, i));
		fputs(i + 1 < count ? ",\n" : "\n", out);
	}
	fputs("  ],\n  \"uses\": [\n", out);
	count = semindex_use_count(s);
	for (size_t i = 0; i < count; i++) {
		print_json_use(out, semindex_get_use(s, i));
		fputs(i + 1 < count ? ",\n" : "\n", out);
	}
	fputs("  ]\n}\n", out);

	return ferror(out) ? -1 : 0;
}
