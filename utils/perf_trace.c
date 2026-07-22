// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "perf_trace.h"

struct semindex_trace {
	int fd;
	char *command;
	char *source;
	int failed;
};

static void trace_error(semindex_trace_t *trace, const char *message)
{
	if (!trace->failed)
		fprintf(stderr, "semindex: trace: %s: %s\n", message, strerror(errno));
	trace->failed = 1;
}

static char *json_escape(const char *value)
{
	static const char hex[] = "0123456789abcdef";
	const unsigned char *p;
	char *escaped;
	char *out;
	size_t len = 0;

	for (p = (const unsigned char *)value; *p; p++) {
		if (*p == '"' || *p == '\\')
			len += 2;
		else if (*p < 0x20 || *p >= 0x80)
			len += 6;
		else
			len++;
	}
	escaped = malloc(len + 1);
	if (!escaped)
		return NULL;

	out = escaped;
	for (p = (const unsigned char *)value; *p; p++) {
		if (*p == '"' || *p == '\\') {
			*out++ = '\\';
			*out++ = *p;
		} else if (*p < 0x20 || *p >= 0x80) {
			*out++ = '\\';
			*out++ = 'u';
			*out++ = '0';
			*out++ = '0';
			*out++ = hex[*p >> 4];
			*out++ = hex[*p & 0xf];
		} else {
			*out++ = *p;
		}
	}
	*out = '\0';
	return escaped;
}

static int monotonic_time(uint64_t *value)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		return -1;
	*value = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
	return 0;
}

semindex_trace_t *semindex_trace_open(const char *path, const char *command, const char *source)
{
	semindex_trace_t *trace;

	if (!path)
		return NULL;
	trace = calloc(1, sizeof(*trace));
	if (!trace) {
		fprintf(stderr, "semindex: failed to allocate trace state\n");
		return NULL;
	}
	trace->fd = -1;
	trace->command = json_escape(command ? command : "");
	trace->source = json_escape(source ? source : "");
	if (!trace->command || !trace->source) {
		fprintf(stderr, "semindex: failed to allocate trace state\n");
		goto fail;
	}
	trace->fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
	if (trace->fd < 0) {
		fprintf(stderr, "semindex: failed to open trace '%s': %s\n", path, strerror(errno));
		goto fail;
	}
	return trace;

fail:
	free(trace->source);
	free(trace->command);
	free(trace);
	return NULL;
}

semindex_trace_time_t semindex_trace_begin(semindex_trace_t *trace)
{
	uint64_t now = 0;

	if (!trace || trace->failed)
		return 0;
	if (monotonic_time(&now) < 0)
		trace_error(trace, "clock_gettime failed");
	return now;
}

static void trace_end(semindex_trace_t *trace, const char *phase, semindex_trace_time_t start, int counted,
	uint64_t items_in, uint64_t items_out)
{
	char *escaped_phase = NULL;
	char *line = NULL;
	uint64_t end;
	ssize_t written;
	int len;

	if (!trace || trace->failed)
		return;
	if (monotonic_time(&end) < 0) {
		trace_error(trace, "clock_gettime failed");
		return;
	}
	escaped_phase = json_escape(phase ? phase : "");
	if (!escaped_phase)
		goto no_memory;
	if (counted) {
		len = snprintf(NULL, 0,
			"{\"pid\":%ld,\"command\":\"%s\",\"source\":\"%s\",\"phase\":\"%s\","
			"\"start_ns\":%llu,\"duration_ns\":%llu,\"items_in\":%llu,\"items_out\":%llu}\n",
			(long)getpid(), trace->command, trace->source, escaped_phase, (unsigned long long)start,
			(unsigned long long)(end - start), (unsigned long long)items_in, (unsigned long long)items_out);
	} else {
		len = snprintf(NULL, 0,
			"{\"pid\":%ld,\"command\":\"%s\",\"source\":\"%s\",\"phase\":\"%s\","
			"\"start_ns\":%llu,\"duration_ns\":%llu}\n",
			(long)getpid(), trace->command, trace->source, escaped_phase, (unsigned long long)start,
			(unsigned long long)(end - start));
	}
	if (len < 0)
		goto format_error;
	line = malloc((size_t)len + 1);
	if (!line)
		goto no_memory;
	if (counted) {
		if (snprintf(line, (size_t)len + 1,
			    "{\"pid\":%ld,\"command\":\"%s\",\"source\":\"%s\",\"phase\":\"%s\","
			    "\"start_ns\":%llu,\"duration_ns\":%llu,\"items_in\":%llu,\"items_out\":%llu}\n",
			    (long)getpid(), trace->command, trace->source, escaped_phase, (unsigned long long)start,
			    (unsigned long long)(end - start), (unsigned long long)items_in,
			    (unsigned long long)items_out) != len)
			goto format_error;
	} else if (snprintf(line, (size_t)len + 1,
			   "{\"pid\":%ld,\"command\":\"%s\",\"source\":\"%s\",\"phase\":\"%s\","
			   "\"start_ns\":%llu,\"duration_ns\":%llu}\n",
			   (long)getpid(), trace->command, trace->source, escaped_phase, (unsigned long long)start,
			   (unsigned long long)(end - start)) != len) {
		goto format_error;
	}
	do {
		written = write(trace->fd, line, len);
	} while (written < 0 && errno == EINTR);
	if (written != len) {
		if (written >= 0)
			errno = EIO;
		trace_error(trace, "write failed");
	}
	free(line);
	free(escaped_phase);
	return;

no_memory:
	errno = ENOMEM;
	trace_error(trace, "allocation failed");
	goto out;
format_error:
	errno = EINVAL;
	trace_error(trace, "formatting failed");
out:
	free(line);
	free(escaped_phase);
}

void semindex_trace_end(semindex_trace_t *trace, const char *phase, semindex_trace_time_t start)
{
	trace_end(trace, phase, start, 0, 0, 0);
}

void semindex_trace_end_counted(semindex_trace_t *trace, const char *phase, semindex_trace_time_t start,
	uint64_t items_in, uint64_t items_out)
{
	trace_end(trace, phase, start, 1, items_in, items_out);
}

int semindex_trace_close(semindex_trace_t *trace)
{
	int failed;

	if (!trace)
		return 0;
	if (trace->fd >= 0 && close(trace->fd) < 0)
		trace_error(trace, "close failed");
	failed = trace->failed;
	free(trace->source);
	free(trace->command);
	free(trace);
	return failed ? -1 : 0;
}
