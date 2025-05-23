/*
 * SPDX-FileCopyrightText: 2013 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#define _LGPL_SOURCE
#include "lttng-ctl-helper.hpp"

#include <common/sessiond-comm/sessiond-comm.hpp>

#include <lttng/lttng-error.h>
#include <lttng/save-internal.hpp>
#include <lttng/save.h>

#include <string.h>

struct lttng_save_session_attr *lttng_save_session_attr_create(void)
{
	return zmalloc<lttng_save_session_attr>();
}

void lttng_save_session_attr_destroy(struct lttng_save_session_attr *output)
{
	if (output) {
		free(output);
	}
}

const char *lttng_save_session_attr_get_session_name(struct lttng_save_session_attr *attr)
{
	const char *ret = nullptr;

	if (attr && attr->session_name[0]) {
		ret = attr->session_name;
	}

	return ret;
}

const char *lttng_save_session_attr_get_output_url(struct lttng_save_session_attr *attr)
{
	const char *ret = nullptr;

	if (attr && attr->configuration_url[0]) {
		ret = attr->configuration_url;
	}

	return ret;
}

int lttng_save_session_attr_get_overwrite(struct lttng_save_session_attr *attr)
{
	return attr ? attr->overwrite : -LTTNG_ERR_INVALID;
}

int lttng_save_session_attr_get_omit_name(struct lttng_save_session_attr *attr)
{
	return attr ? attr->omit_name : -LTTNG_ERR_INVALID;
}

int lttng_save_session_attr_get_omit_output(struct lttng_save_session_attr *attr)
{
	return attr ? attr->omit_output : -LTTNG_ERR_INVALID;
}

int lttng_save_session_attr_set_session_name(struct lttng_save_session_attr *attr,
					     const char *session_name)
{
	int ret = 0;

	if (!attr) {
		ret = -LTTNG_ERR_INVALID;
		goto error;
	}

	if (session_name) {
		size_t len;

		len = strlen(session_name);
		if (len >= LTTNG_NAME_MAX) {
			ret = -LTTNG_ERR_INVALID;
			goto error;
		}

		ret = lttng_strncpy(attr->session_name, session_name, sizeof(attr->session_name));
		if (ret) {
			ret = -LTTNG_ERR_INVALID;
			goto error;
		}
	} else {
		attr->session_name[0] = '\0';
	}
error:
	return ret;
}

int lttng_save_session_attr_set_output_url(struct lttng_save_session_attr *attr, const char *url)
{
	int ret = 0;
	size_t len;
	ssize_t size;
	struct lttng_uri *uris = nullptr;

	if (!attr) {
		ret = -LTTNG_ERR_INVALID;
		goto error;
	}

	if (!url) {
		attr->configuration_url[0] = '\0';
		ret = 0;
		goto end;
	}

	len = strlen(url);
	if (len >= PATH_MAX) {
		ret = -LTTNG_ERR_INVALID;
		goto error;
	}

	size = uri_parse_str_urls(url, nullptr, &uris);
	if (size <= 0 || uris[0].dtype != LTTNG_DST_PATH) {
		ret = -LTTNG_ERR_INVALID;
		goto error;
	}

	/* Copy string plus the NULL terminated byte. */
	ret = lttng_strncpy(
		attr->configuration_url, uris[0].dst.path, sizeof(attr->configuration_url));
	if (ret) {
		ret = -LTTNG_ERR_INVALID;
		goto error;
	}

end:
error:
	free(uris);
	return ret;
}

int lttng_save_session_attr_set_overwrite(struct lttng_save_session_attr *attr, int overwrite)
{
	int ret = 0;

	if (!attr) {
		ret = -LTTNG_ERR_INVALID;
		goto end;
	}

	attr->overwrite = !!overwrite;
end:
	return ret;
}

int lttng_save_session_attr_set_omit_name(struct lttng_save_session_attr *attr, int omit_name)
{
	int ret = 0;

	if (!attr) {
		ret = -LTTNG_ERR_INVALID;
		goto end;
	}

	attr->omit_name = !!omit_name;
end:
	return ret;
}

int lttng_save_session_attr_set_omit_output(struct lttng_save_session_attr *attr, int omit_output)
{
	int ret = 0;

	if (!attr) {
		ret = -LTTNG_ERR_INVALID;
		goto end;
	}

	attr->omit_output = !!omit_output;
end:
	return ret;
}

/*
 * The lttng-ctl API does not expose all the information needed to save the
 * session configurations. Thus, we must send a save command to the session
 * daemon which will, in turn, save its current session configuration.
 */
int lttng_save_session(struct lttng_save_session_attr *attr)
{
	struct lttcomm_session_msg lsm;
	int ret;

	if (!attr) {
		ret = -LTTNG_ERR_INVALID;
		goto end;
	}

	memset(&lsm, 0, sizeof(lsm));
	lsm.cmd_type = LTTCOMM_SESSIOND_COMMAND_SAVE_SESSION;

	memcpy(&lsm.u.save_session.attr, attr, sizeof(struct lttng_save_session_attr));
	ret = lttng_ctl_ask_sessiond(&lsm, nullptr);
end:
	return ret;
}
