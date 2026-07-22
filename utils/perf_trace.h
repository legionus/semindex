// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEMINDEX_PERF_TRACE_H
#define SEMINDEX_PERF_TRACE_H

#include <stdint.h>

typedef struct semindex_trace semindex_trace_t;
typedef uint64_t semindex_trace_time_t;

semindex_trace_t *semindex_trace_open(const char *path, const char *command, const char *source);
semindex_trace_time_t semindex_trace_begin(semindex_trace_t *trace);
void semindex_trace_end(semindex_trace_t *trace, const char *phase, semindex_trace_time_t start);
void semindex_trace_end_counted(semindex_trace_t *trace, const char *phase, semindex_trace_time_t start,
	uint64_t items_in, uint64_t items_out);
int semindex_trace_close(semindex_trace_t *trace);

#endif /* SEMINDEX_PERF_TRACE_H */
