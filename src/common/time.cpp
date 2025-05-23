/*
 * SPDX-FileCopyrightText: 2013 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#include <common/compat/errno.hpp>
#include <common/error.hpp>
#include <common/exception.hpp>
#include <common/macros.hpp>
#include <common/time.hpp>

#include <algorithm>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool utf8_output_supported;

bool locale_supports_utf8()
{
	return utf8_output_supported;
}

int timespec_to_ms(struct timespec ts, unsigned long *ms)
{
	unsigned long res, remain_ms;

	if (ts.tv_sec > ULONG_MAX / MSEC_PER_SEC) {
		errno = EOVERFLOW;
		return -1; /* multiplication overflow */
	}
	res = ts.tv_sec * MSEC_PER_SEC;
	remain_ms = ULONG_MAX - res;
	if (ts.tv_nsec / NSEC_PER_MSEC > remain_ms) {
		errno = EOVERFLOW;
		return -1; /* addition overflow */
	}
	res += ts.tv_nsec / NSEC_PER_MSEC;
	*ms = res;
	return 0;
}

struct timespec timespec_abs_diff(struct timespec t1, struct timespec t2)
{
	const uint64_t ts1 = (uint64_t) t1.tv_sec * (uint64_t) NSEC_PER_SEC + (uint64_t) t1.tv_nsec;
	const uint64_t ts2 = (uint64_t) t2.tv_sec * (uint64_t) NSEC_PER_SEC + (uint64_t) t2.tv_nsec;
	const uint64_t diff = std::max(ts1, ts2) - std::min(ts1, ts2);
	struct timespec res;

	res.tv_sec = diff / (uint64_t) NSEC_PER_SEC;
	res.tv_nsec = diff % (uint64_t) NSEC_PER_SEC;
	return res;
}

static void __attribute__((constructor)) init_locale_utf8_support()
{
	const char *program_locale = setlocale(LC_ALL, nullptr);
	const char *lang = getenv("LANG");

	if (program_locale && strstr(program_locale, "utf8")) {
		utf8_output_supported = true;
	} else if (lang && strstr(lang, "utf8")) {
		utf8_output_supported = true;
	}
}

int time_to_iso8601_str(time_t time, char *str, size_t len)
{
	int ret = 0;
	struct tm *tm_result;
	struct tm tm_storage;
	size_t strf_ret;

	if (len < ISO8601_STR_LEN) {
		ERR("Buffer too short to format ISO 8601 timestamp: %zu bytes provided when at least %zu are needed",
		    len,
		    ISO8601_STR_LEN);
		ret = -1;
		goto end;
	}

	tm_result = localtime_r(&time, &tm_storage);
	if (!tm_result) {
		ret = -1;
		PERROR("Failed to break down timestamp to tm structure");
		goto end;
	}

	strf_ret = strftime(str, len, "%Y%m%dT%H%M%S%z", tm_result);
	if (strf_ret == 0) {
		ret = -1;
		ERR("Failed to format timestamp as local time");
		goto end;
	}
end:
	return ret;
}

int time_to_datetime_str(time_t time, char *str, size_t len)
{
	int ret = 0;
	struct tm *tm_result;
	struct tm tm_storage;
	size_t strf_ret;

	if (len < DATETIME_STR_LEN) {
		ERR("Buffer too short to format to datetime: %zu bytes provided when at least %zu are needed",
		    len,
		    DATETIME_STR_LEN);
		ret = -1;
		goto end;
	}

	tm_result = localtime_r(&time, &tm_storage);
	if (!tm_result) {
		ret = -1;
		PERROR("Failed to break down timestamp to tm structure");
		goto end;
	}

	strf_ret = strftime(str, len, "%Y%m%d-%H%M%S", tm_result);
	if (strf_ret == 0) {
		ret = -1;
		ERR("Failed to format timestamp as local time");
		goto end;
	}
end:
	return ret;
}

std::string lttng::utils::time_to_iso8601_str(std::time_t time)
{
	std::string iso8601_str(ISO8601_STR_LEN, '\0');
	const auto ret = ::time_to_iso8601_str(time, &iso8601_str[0], iso8601_str.capacity());

	if (ret) {
		LTTNG_THROW_ERROR("Failed to format time to iso8601 format");
	}

	/* Don't include '\0' in the C++ string. */
	iso8601_str.resize(iso8601_str.size() - 1);

	return iso8601_str;
}
