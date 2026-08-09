// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_OUTPUT_H
#define SEMINDEX_OUTPUT_H

#include <stdio.h>

#include "semindex.h"

enum output_format {
	FORMAT_DISSECT,
	FORMAT_JSON,
};

int output_dissect(FILE *out, semindex_t *s);
int output_json(FILE *out, semindex_t *s);
#endif /* SEMINDEX_OUTPUT_H */
