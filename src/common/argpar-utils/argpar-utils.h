/*
 * Copyright (C) 2021 Simon Marchi <simon.marchi@efficios.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef COMMON_ARGPAR_UTILS_H
#define COMMON_ARGPAR_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include <common/macros.h>
#include <common/argpar/argpar.h>
#include <common/string-utils/format.h>

#define WHILE_PARSING_ARG_N_ARG_FMT "While parsing argument #%d (`%s`): "

enum parse_next_item_status
{
	PARSE_NEXT_ITEM_STATUS_OK = 0,
	PARSE_NEXT_ITEM_STATUS_END = 1,
	PARSE_NEXT_ITEM_STATUS_ERROR = -1,
};

/*
 * Parse the next argpar item using `iter`.
 *
 * The item in `*item` is always freed and cleared on entry.
 *
 * If an item is parsed successfully, return the new item in `*item` and return
 * PARSE_NEXT_ITEM_STATUS_OK.
 *
 * If the end of the argument list is reached, return
 * PARSE_NEXT_ITEM_STATUS_END.
 *
 * On error, print a descriptive error message and return
 * PARSE_NEXT_ITEM_STATUS_ERROR.  If `context_fmt` is non-NULL, it is formatted
 * using the following arguments and prepended to the error message.
 * Add `argc_offset` to the argument index mentioned in the error message.
 *
 * If `unknown_opt_is_error` is true, an unknown option is considered an error.
 * Otherwise, it is considered as the end of the argument list.
 */
ATTR_FORMAT_PRINTF(6, 7)
enum parse_next_item_status parse_next_item(struct argpar_iter *iter,
		const struct argpar_item **item, int argc_offset,
		const char **argv, bool unknown_opt_is_error,
		const char *context_fmt, ...);

#ifdef __cplusplus
}
#endif
#endif
