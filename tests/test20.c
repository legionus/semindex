// SPDX-License-Identifier: GPL-2.0-or-later
#include "missing-test20.h"

struct Request {
	int value;
};

int update_request(void)
{
	struct Request request;

	request = (struct Request){ .value = 1 };

	return request.value;
}
