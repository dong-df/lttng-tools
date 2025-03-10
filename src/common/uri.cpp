/*
 * SPDX-FileCopyrightText: 2012 David Goulet <dgoulet@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#define _LGPL_SOURCE
#include "uri.hpp"

#include <common/common.hpp>
#include <common/compat/netdb.hpp>
#include <common/defaults.hpp>
#include <common/utils.hpp>

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define LOOPBACK_ADDR_IPV4 "127.0.0.1"
#define LOOPBACK_ADDR_IPV6 "::1"

enum uri_proto_code {
	P_NET,
	P_NET6,
	P_FILE,
	P_TCP,
	P_TCP6,
};

namespace {
struct uri_proto {
	const char *name;
	const char *leading_string;
	enum uri_proto_code code;
	enum lttng_proto_type type;
	enum lttng_dst_type dtype;
};

/* Supported protocols */
const struct uri_proto proto_uri[] = { { .name = "file",
					 .leading_string = "file://",
					 .code = P_FILE,
					 .type = LTTNG_PROTO_TYPE_NONE,
					 .dtype = LTTNG_DST_PATH },
				       { .name = "net",
					 .leading_string = "net://",
					 .code = P_NET,
					 .type = LTTNG_TCP,
					 .dtype = LTTNG_DST_IPV4 },
				       { .name = "net4",
					 .leading_string = "net4://",
					 .code = P_NET,
					 .type = LTTNG_TCP,
					 .dtype = LTTNG_DST_IPV4 },
				       { .name = "net6",
					 .leading_string = "net6://",
					 .code = P_NET6,
					 .type = LTTNG_TCP,
					 .dtype = LTTNG_DST_IPV6 },
				       { .name = "tcp",
					 .leading_string = "tcp://",
					 .code = P_TCP,
					 .type = LTTNG_TCP,
					 .dtype = LTTNG_DST_IPV4 },
				       { .name = "tcp4",
					 .leading_string = "tcp4://",
					 .code = P_TCP,
					 .type = LTTNG_TCP,
					 .dtype = LTTNG_DST_IPV4 },
				       { .name = "tcp6",
					 .leading_string = "tcp6://",
					 .code = P_TCP6,
					 .type = LTTNG_TCP,
					 .dtype = LTTNG_DST_IPV6 },
				       /* Invalid proto marking the end of the array. */
				       {} };
} /* namespace */

/*
 * Return pointer to the character in s matching one of the characters in
 * accept. If nothing is found, return pointer to the end of string (eos).
 */
static inline const char *strpbrk_or_eos(const char *s, const char *accept)
{
	char *p = (char *) strpbrk(s, accept);
	if (p == nullptr) {
		p = (char *) strchr(s, '\0');
	}

	return p;
}

/*
 * Validate if proto is a supported protocol from proto_uri array.
 */
static const struct uri_proto *get_uri_proto(const char *uri_str)
{
	const struct uri_proto *supported = nullptr;

	/* Safety net */
	if (uri_str == nullptr) {
		goto end;
	}

	for (supported = &proto_uri[0]; supported->leading_string != nullptr; ++supported) {
		if (strncasecmp(uri_str,
				supported->leading_string,
				strlen(supported->leading_string)) == 0) {
			goto end;
		}
	}

	/* Proto not found */
	return nullptr;

end:
	return supported;
}

/*
 * Set network address from string into dst. Supports both IP string and
 * hostname.
 */
static int set_ip_address(const char *addr, int af, char *dst, size_t size)
{
	int ret;
	unsigned char buf[sizeof(struct in6_addr)];
	struct hostent *record;

	LTTNG_ASSERT(addr);
	LTTNG_ASSERT(dst);

	memset(dst, 0, size);

	/* Network protocol */
	ret = inet_pton(af, addr, buf);
	if (ret < 1) {
		/* We consider the dst to be an hostname or an invalid IP char */
		record = lttng_gethostbyname2(addr, af);
		if (record) {
			/* Translate IP to string */
			if (!inet_ntop(af, record->h_addr_list[0], dst, size)) {
				PERROR("inet_ntop");
				goto error;
			}
		} else if (!strcmp(addr, "localhost") && (af == AF_INET || af == AF_INET6)) {
			/*
			 * Some systems may not have "localhost" defined in
			 * accordance with IETF RFC 6761. According to this RFC,
			 * applications may recognize "localhost" names as
			 * special and resolve to the appropriate loopback
			 * address.
			 *
			 * We choose to use the system name resolution API first
			 * to honor its network configuration. If this fails, we
			 * resolve to the appropriate loopback address. This is
			 * done to accommodates systems which may want to start
			 * tracing before their network configured.
			 */
			const char *loopback_addr = af == AF_INET ? LOOPBACK_ADDR_IPV4 :
								    LOOPBACK_ADDR_IPV6;
			const size_t loopback_addr_len = af == AF_INET ?
				sizeof(LOOPBACK_ADDR_IPV4) :
				sizeof(LOOPBACK_ADDR_IPV6);

			DBG2("Could not resolve localhost address, using fallback");
			if (loopback_addr_len > size) {
				ERR("Could not resolve localhost address; destination string is too short");
				goto error;
			}
			strcpy(dst, loopback_addr);
		} else {
			/* At this point, the IP or the hostname is bad */
			goto error;
		}
	} else {
		if (size > 0) {
			strncpy(dst, addr, size);
			dst[size - 1] = '\0';
		}
	}

	DBG2("IP address resolved to %s", dst);
	return 0;

error:
	ERR("URI parse bad hostname %s for af %d", addr, af);
	return -1;
}

/*
 * Set default URI attribute which is basically the given stream type and the
 * default port if none is set in the URI.
 */
static void set_default_uri_attr(struct lttng_uri *uri, enum lttng_stream_type stype)
{
	uri->stype = stype;
	if (uri->dtype != LTTNG_DST_PATH && uri->port == 0) {
		uri->port = (stype == LTTNG_STREAM_CONTROL) ? DEFAULT_NETWORK_CONTROL_PORT :
							      DEFAULT_NETWORK_DATA_PORT;
	}
}

/*
 * Compare two URL destination.
 *
 * Return 0 is equal else is not equal.
 */
static int compare_destination(struct lttng_uri *ctrl, struct lttng_uri *data)
{
	int ret;

	LTTNG_ASSERT(ctrl);
	LTTNG_ASSERT(data);

	switch (ctrl->dtype) {
	case LTTNG_DST_IPV4:
		ret = strncmp(ctrl->dst.ipv4, data->dst.ipv4, sizeof(ctrl->dst.ipv4));
		break;
	case LTTNG_DST_IPV6:
		ret = strncmp(ctrl->dst.ipv6, data->dst.ipv6, sizeof(ctrl->dst.ipv6));
		break;
	default:
		ret = -1;
		break;
	}

	return ret;
}

/*
 * Build a string URL from a lttng_uri object.
 */
int uri_to_str_url(struct lttng_uri *uri, char *dst, size_t size)
{
	int ipver, ret;
	const char *addr;
	char proto[5], port[7];

	LTTNG_ASSERT(uri);
	LTTNG_ASSERT(dst);

	if (uri->dtype == LTTNG_DST_PATH) {
		ipver = 0;
		addr = uri->dst.path;
		(void) snprintf(proto, sizeof(proto), "file");
		(void) snprintf(port, sizeof(port), "%s", "");
	} else {
		ipver = (uri->dtype == LTTNG_DST_IPV4) ? 4 : 6;
		addr = (ipver == 4) ? uri->dst.ipv4 : uri->dst.ipv6;
		(void) snprintf(proto, sizeof(proto), "tcp%d", ipver);
		(void) snprintf(port, sizeof(port), ":%d", uri->port);
	}

	ret = snprintf(dst,
		       size,
		       "%s://%s%s%s%s/%s",
		       proto,
		       (ipver == 6) ? "[" : "",
		       addr,
		       (ipver == 6) ? "]" : "",
		       port,
		       uri->subdir);
	if (ret < 0) {
		PERROR("snprintf uri to url");
	}

	return ret;
}

/*
 * Compare two URIs.
 *
 * Return 0 if equal else 1.
 */
int uri_compare(struct lttng_uri *uri1, struct lttng_uri *uri2)
{
	return memcmp(uri1, uri2, sizeof(struct lttng_uri));
}

/*
 * Free URI memory.
 */
void uri_free(struct lttng_uri *uri)
{
	free(uri);
}

/*
 * Parses a string URI to a lttng_uri. This function can potentially return
 * more than one URI in uris so the size of the array is returned and uris is
 * allocated and populated. Caller must free(3) the array.
 *
 * This function can not detect the stream type of the URI so the caller has to
 * make sure the correct type (stype) is set on the return URI(s). The default
 * port must also be set by the caller if the returned URI has its port set to
 * zero.
 *
 * NOTE: A good part of the following code was inspired from the "wget" source
 * tree from the src/url.c file and url_parse() function. Also, the
 * strpbrk_or_eos() function found above is also inspired by the same code.
 * This code was originally licensed GPLv2 so we acknolwedge the Free Software
 * Foundation here for the work and to make sure we are compliant with it.
 */
ssize_t uri_parse(const char *str_uri, struct lttng_uri **uris)
{
	int ret, i = 0;
	/* Size of the uris array. Default is 1 */
	ssize_t size = 1;
	char subdir[PATH_MAX];
	unsigned int ctrl_port = 0;
	unsigned int data_port = 0;
	struct lttng_uri *tmp_uris;
	char *addr_f = nullptr;
	const struct uri_proto *proto;
	const char *purl, *addr_e, *addr_b, *subdir_b = nullptr;
	const char *seps = ":/\0";

	/*
	 * The first part is the protocol portion of a maximum of 5 bytes for now.
	 * The second part is the hostname or IP address. The 255 bytes size is the
	 * limit found in the RFC 1035 for the total length of a domain name
	 * (https://www.ietf.org/rfc/rfc1035.txt). Finally, for the net://
	 * protocol, two ports CAN be specified.
	 */

	DBG3("URI string: %s", str_uri);

	proto = get_uri_proto(str_uri);
	if (proto == nullptr) {
		ERR("URI parse unknown protocol %s", str_uri);
		goto error;
	}

	purl = str_uri;

	if (proto->code == P_NET || proto->code == P_NET6) {
		/* Special case for net:// which requires two URI objects */
		size = 2;
	}

	/* Allocate URI array */
	tmp_uris = calloc<lttng_uri>(size);
	if (tmp_uris == nullptr) {
		PERROR("zmalloc uri");
		goto error;
	}

	memset(subdir, 0, sizeof(subdir));
	purl += strlen(proto->leading_string);

	/* Copy known value to the first URI. */
	tmp_uris[0].dtype = proto->dtype;
	tmp_uris[0].proto = proto->type;

	if (proto->code == P_FILE) {
		if (*purl != '/') {
			ERR("Missing destination full path.");
			goto free_error;
		}

		strncpy(tmp_uris[0].dst.path, purl, sizeof(tmp_uris[0].dst.path));
		tmp_uris[0].dst.path[sizeof(tmp_uris[0].dst.path) - 1] = '\0';
		DBG3("URI file destination: %s", purl);
		goto end;
	}

	/* Assume we are at the beginning of an address or host of some sort. */
	addr_b = purl;

	/*
	 * Handle IPv6 address inside square brackets as mention by RFC 2732. IPv6
	 * address that does not start AND end with brackets will be rejected even
	 * if valid.
	 *
	 * proto://[<addr>]...
	 *         ^
	 */
	if (*purl == '[') {
		/* Address begins after '[' */
		addr_b = purl + 1;
		addr_e = strchr(addr_b, ']');
		if (addr_e == nullptr || addr_b == addr_e) {
			ERR("Broken IPv6 address %s", addr_b);
			goto free_error;
		}

		/* Moving parsed URL pointer after the final bracket ']' */
		purl = addr_e + 1;

		/*
		 * The closing bracket must be followed by a seperator or NULL char.
		 */
		if (strchr(seps, *purl) == nullptr) {
			ERR("Unknown symbol after IPv6 address: %s", purl);
			goto free_error;
		}
	} else {
		purl = strpbrk_or_eos(purl, seps);
		addr_e = purl;
	}

	/* Check if we at least have a char for the addr or hostname. */
	if (addr_b == addr_e) {
		ERR("No address or hostname detected.");
		goto free_error;
	}

	addr_f = utils_strdupdelim(addr_b, addr_e);
	if (addr_f == nullptr) {
		goto free_error;
	}

	/*
	 * Detect PORT after address. The net/net6 protocol allows up to two port
	 * so we can define the control and data port.
	 */
	while (*purl == ':') {
		const char *port_b, *port_e;
		char *port_f;

		/* Update pass counter */
		i++;

		/*
		 * Maximum of two ports is possible if P_NET/NET6. Bigger than that,
		 * two much stuff.
		 */
		if ((i == 2 && (proto->code != P_NET && proto->code != P_NET6)) || i > 2) {
			break;
		}

		/*
		 * Move parsed URL to port value.
		 * proto://addr_host:PORT1:PORT2/foo/bar
		 *                   ^
		 */
		++purl;
		port_b = purl;
		purl = strpbrk_or_eos(purl, seps);
		port_e = purl;

		if (port_b != port_e) {
			int port;

			port_f = utils_strdupdelim(port_b, port_e);
			if (port_f == nullptr) {
				goto free_error;
			}

			port = atoi(port_f);
			if (port > 0xffff || port < 0x0) {
				ERR("Invalid port number %d", port);
				free(port_f);
				goto free_error;
			}
			free(port_f);

			if (i == 1) {
				ctrl_port = port;
			} else {
				data_port = port;
			}
		}
	};

	/* Check for a valid subdir or trailing garbage */
	if (*purl == '/') {
		/*
		 * Move to subdir value.
		 * proto://addr_host:PORT1:PORT2/foo/bar
		 *                               ^
		 */
		++purl;
		subdir_b = purl;
	} else if (*purl != '\0') {
		ERR("Trailing characters not recognized: %s", purl);
		goto free_error;
	}

	/* We have enough valid information to create URI(s) object */

	/* Copy generic information */
	tmp_uris[0].port = ctrl_port;

	/* Copy subdirectory if one. */
	if (subdir_b) {
		strncpy(tmp_uris[0].subdir, subdir_b, sizeof(tmp_uris[0].subdir));
		tmp_uris[0].subdir[sizeof(tmp_uris[0].subdir) - 1] = '\0';
	}

	switch (proto->code) {
	case P_NET:
		ret = set_ip_address(
			addr_f, AF_INET, tmp_uris[0].dst.ipv4, sizeof(tmp_uris[0].dst.ipv4));
		if (ret < 0) {
			goto free_error;
		}

		memcpy(tmp_uris[1].dst.ipv4, tmp_uris[0].dst.ipv4, sizeof(tmp_uris[1].dst.ipv4));

		tmp_uris[1].dtype = proto->dtype;
		tmp_uris[1].proto = proto->type;
		tmp_uris[1].port = data_port;
		break;
	case P_NET6:
		ret = set_ip_address(
			addr_f, AF_INET6, tmp_uris[0].dst.ipv6, sizeof(tmp_uris[0].dst.ipv6));
		if (ret < 0) {
			goto free_error;
		}

		memcpy(tmp_uris[1].dst.ipv6, tmp_uris[0].dst.ipv6, sizeof(tmp_uris[1].dst.ipv6));

		tmp_uris[1].dtype = proto->dtype;
		tmp_uris[1].proto = proto->type;
		tmp_uris[1].port = data_port;
		break;
	case P_TCP:
		ret = set_ip_address(
			addr_f, AF_INET, tmp_uris[0].dst.ipv4, sizeof(tmp_uris[0].dst.ipv4));
		if (ret < 0) {
			goto free_error;
		}
		break;
	case P_TCP6:
		ret = set_ip_address(
			addr_f, AF_INET6, tmp_uris[0].dst.ipv6, sizeof(tmp_uris[0].dst.ipv6));
		if (ret < 0) {
			goto free_error;
		}
		break;
	default:
		goto free_error;
	}

end:
	DBG3("URI dtype: %d, proto: %d, host: %s, subdir: %s, ctrl: %d, data: %d",
	     proto->dtype,
	     proto->type,
	     (addr_f == NULL) ? "" : addr_f,
	     (subdir_b == NULL) ? "" : subdir_b,
	     ctrl_port,
	     data_port);

	free(addr_f);

	*uris = tmp_uris;
	LTTNG_ASSERT(size == 1 || size == 2);
	return size;

free_error:
	free(addr_f);
	free(tmp_uris);
error:
	return -1;
}

/*
 * Parse a string URL and creates URI(s) returning the size of the populated
 * array.
 */
ssize_t uri_parse_str_urls(const char *ctrl_url, const char *data_url, struct lttng_uri **uris)
{
	unsigned int equal = 1, idx = 0;
	/* Add the "file://" size to the URL maximum size */
	char url[PATH_MAX + 7];
	ssize_t ctrl_uri_count = 0, data_uri_count = 0, uri_count;
	struct lttng_uri *ctrl_uris = nullptr, *data_uris = nullptr;
	struct lttng_uri *tmp_uris = nullptr;

	/* No URL(s) is allowed. This means that the consumer will be disabled. */
	if (ctrl_url == nullptr && data_url == nullptr) {
		return 0;
	}

	/* Check if URLs are equal and if so, only use the control URL */
	if ((ctrl_url && *ctrl_url != '\0') && (data_url && *data_url != '\0')) {
		equal = !strcmp(ctrl_url, data_url);
	}

	/*
	 * Since we allow the str_url to be a full local filesystem path, we are
	 * going to create a valid file:// URL if it's the case.
	 *
	 * Check if first character is a '/' or else reject the URL.
	 */
	if (ctrl_url && ctrl_url[0] == '/') {
		int ret;

		ret = snprintf(url, sizeof(url), "file://%s", ctrl_url);
		if (ret < 0) {
			PERROR("snprintf file url");
			goto parse_error;
		} else if (ret >= sizeof(url)) {
			PERROR("snprintf file url is too long");
			goto parse_error;
		}
		ctrl_url = url;
	}

	/* Parse the control URL if there is one */
	if (ctrl_url && *ctrl_url != '\0') {
		ctrl_uri_count = uri_parse(ctrl_url, &ctrl_uris);
		if (ctrl_uri_count < 1) {
			ERR("Unable to parse the URL %s", ctrl_url);
			goto parse_error;
		}

		/* 1 and 2 are the only expected values on success. */
		LTTNG_ASSERT(ctrl_uri_count == 1 || ctrl_uri_count == 2);

		/* At this point, we know there is at least one URI in the array */
		set_default_uri_attr(&ctrl_uris[0], LTTNG_STREAM_CONTROL);

		if (ctrl_uris[0].dtype == LTTNG_DST_PATH && (data_url && *data_url != '\0')) {
			ERR("Cannot have a data URL when destination is file://");
			goto error;
		}

		/* URL are not equal but the control URL uses a net:// protocol */
		if (ctrl_uri_count == 2) {
			if (!equal) {
				ERR("Control URL uses the net:// protocol and the data URL is "
				    "different. Not allowed.");
				goto error;
			} else {
				set_default_uri_attr(&ctrl_uris[1], LTTNG_STREAM_DATA);
				/*
				 * The data_url and ctrl_url are equal and the ctrl_url
				 * contains a net:// protocol so we just skip the data part.
				 */
				data_url = nullptr;
			}
		}
	}

	if (data_url && *data_url != '\0') {
		int ret;

		/* We have to parse the data URL in this case */
		data_uri_count = uri_parse(data_url, &data_uris);
		if (data_uri_count < 1) {
			ERR("Unable to parse the URL %s", data_url);
			goto error;
		} else if (data_uri_count == 2) {
			ERR("Data URL can not be set with the net[4|6]:// protocol");
			goto error;
		} else {
			/* 1 and 2 are the only expected values on success. */
			LTTNG_ASSERT(data_uri_count == 1);
		}

		set_default_uri_attr(&data_uris[0], LTTNG_STREAM_DATA);

		if (ctrl_uris) {
			ret = compare_destination(&ctrl_uris[0], &data_uris[0]);
			if (ret != 0) {
				ERR("Control and data destination mismatch");
				goto error;
			}
		}
	}

	/* Compute total size. */
	uri_count = ctrl_uri_count + data_uri_count;
	if (uri_count <= 0) {
		goto error;
	}

	tmp_uris = calloc<lttng_uri>(uri_count);
	if (tmp_uris == nullptr) {
		PERROR("zmalloc uris");
		goto error;
	}

	if (ctrl_uris) {
		/* It's possible the control URIs array contains more than one URI */
		memcpy(tmp_uris, ctrl_uris, sizeof(struct lttng_uri) * ctrl_uri_count);
		++idx;
		free(ctrl_uris);
	}

	if (data_uris) {
		memcpy(&tmp_uris[idx], data_uris, sizeof(struct lttng_uri));
		free(data_uris);
	}

	*uris = tmp_uris;

	return uri_count;

error:
	free(ctrl_uris);
	free(data_uris);
	free(tmp_uris);
parse_error:
	return -1;
}
