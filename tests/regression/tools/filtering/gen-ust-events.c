/*
 * SPDX-FileCopyrightText: 2012 David Goulet <dgoulet@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TRACEPOINT_DEFINE
#include "tp.h"

int main(int argc, char **argv)
{
	int i, netint;
	long values[] = { 1, 2, 3 };
	uint32_t net_values[] = { 1, 2, 3 };
	char text[10] = "test";
	char escape[10] = "\\*";
	double dbl = 2.0;
	float flt = 2222.0;
	/* Generate 30 events. */
	int nr_iter = 100;
	useconds_t nr_usec = 0;

	if (argc >= 2) {
		nr_iter = atoi(argv[1]);
	}

	if (argc == 3) {
		/* By default, don't wait unless user specifies. */
		nr_usec = atoi(argv[2]);
	}

	for (i = 0; i < 3; i++) {
		net_values[i] = htonl(net_values[i]);
	}

	for (i = 0; i < nr_iter; i++) {
		netint = htonl(i);
		tracepoint(tp,
			   tptest,
			   i,
			   netint,
			   values,
			   text,
			   strlen(text),
			   escape,
			   dbl,
			   flt,
			   net_values);
		usleep(nr_usec);
	}

	return 0;
}
