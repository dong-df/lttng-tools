/*
 * SPDX-FileCopyrightText: 2011 EfficiOS Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#define _LGPL_SOURCE
#include "command.hpp"
#include "conf.hpp"
#include "exception.hpp"
#include "utils.hpp"

#include <common/defaults.hpp>
#include <common/error.hpp>
#include <common/exception.hpp>
#include <common/make-unique-wrapper.hpp>
#include <common/utils.hpp>

#include <arpa/inet.h>
#include <ctype.h>
#include <fnmatch.h>
#include <inttypes.h>
#include <iostream>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const char *str_all = "ALL";
static const char *str_tracepoint = "Tracepoint";
static const char *str_syscall = "Syscall";
static const char *str_probe = "Probe";
static const char *str_userspace_probe = "Userspace Probe";
static const char *str_function = "Function";

static char *_get_session_name(int quiet)
{
	const char *path;
	char *session_name = nullptr;

	/* Get path to config file */
	path = utils_get_home_dir();
	if (path == nullptr) {
		goto error;
	}

	/* Get session name from config */
	session_name = quiet ? config_read_session_name_quiet(path) :
			       config_read_session_name(path);
	if (session_name == nullptr) {
		goto error;
	}

	DBG2("Config file path found: %s", path);
	DBG("Session name found: %s", session_name);
	return session_name;

error:
	return nullptr;
}

/*
 *  get_session_name
 *
 *  Return allocated string with the implicit session name (found in
 *  the config directory).
 *
 *  The caller must explicitly free the memory associated with the
 *  returned string.
 */
char *get_session_name()
{
	return _get_session_name(0);
}

/*
 *  get_session_name_quiet (no warnings/errors emitted)
 *
 *  Return allocated string with the session name found in the config
 *  directory.
 */
char *get_session_name_quiet()
{
	return _get_session_name(1);
}

/*
 *  list_commands
 *
 *  List commands line by line. This is mostly for bash auto completion and to
 *  avoid difficult parsing.
 */
void list_commands(struct cmd_struct *commands, FILE *ofp)
{
	int i = 0;
	struct cmd_struct *cmd = nullptr;

	cmd = &commands[i];
	while (cmd->name != nullptr) {
		fprintf(ofp, "%s\n", cmd->name);
		i++;
		cmd = &commands[i];
	}
}

/*
 * list_cmd_options
 *
 * Prints a simple list of the options available to a command. This is intended
 * to be easily parsed for bash completion.
 */
void list_cmd_options(FILE *ofp, struct poptOption *options)
{
	int i;
	struct poptOption *option = nullptr;

	for (i = 0; options[i].longName != nullptr; i++) {
		option = &options[i];

		fprintf(ofp, "--%s\n", option->longName);

		if (isprint(option->shortName)) {
			fprintf(ofp, "-%c\n", option->shortName);
		}
	}
}

/*
 * Same as list_cmd_options, but for options specified for argpar.
 */
void list_cmd_options_argpar(FILE *ofp, const struct argpar_opt_descr *options)
{
	int i;

	for (i = 0; options[i].long_name != nullptr; i++) {
		const struct argpar_opt_descr *option = &options[i];

		fprintf(ofp, "--%s\n", option->long_name);

		if (isprint(option->short_name)) {
			fprintf(ofp, "-%c\n", option->short_name);
		}
	}
}

/*
 * fls: returns the position of the most significant bit.
 * Returns 0 if no bit is set, else returns the position of the most
 * significant bit (from 1 to 32 on 32-bit, from 1 to 64 on 64-bit).
 */
#if defined(__i386) || defined(__x86_64)
static inline unsigned int fls_u32(uint32_t x)
{
	int r;

	asm("bsrl %1,%0\n\t"
	    "jnz 1f\n\t"
	    "movl $-1,%0\n\t"
	    "1:\n\t"
	    : "=r"(r)
	    : "rm"(x));
	return r + 1;
}
#define HAS_FLS_U32
#endif

#if defined(__x86_64) && defined(__LP64__)
static inline unsigned int fls_u64(uint64_t x)
{
	long r;

	asm("bsrq %1,%0\n\t"
	    "jnz 1f\n\t"
	    "movq $-1,%0\n\t"
	    "1:\n\t"
	    : "=r"(r)
	    : "rm"(x));
	return r + 1;
}
#define HAS_FLS_U64
#endif

#ifndef HAS_FLS_U64
static __attribute__((unused)) unsigned int fls_u64(uint64_t x)
{
	unsigned int r = 64;

	if (!x)
		return 0;

	if (!(x & 0xFFFFFFFF00000000ULL)) {
		x <<= 32;
		r -= 32;
	}
	if (!(x & 0xFFFF000000000000ULL)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xFF00000000000000ULL)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xF000000000000000ULL)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xC000000000000000ULL)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x8000000000000000ULL)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}
#endif

#ifndef HAS_FLS_U32
static __attribute__((unused)) unsigned int fls_u32(uint32_t x)
{
	unsigned int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xFFFF0000U)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xFF000000U)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xF0000000U)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xC0000000U)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000U)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}
#endif

static unsigned int fls_ulong(unsigned long x)
{
#if (CAA_BITS_PER_LONG == 32)
	return fls_u32(x);
#else
	return fls_u64(x);
#endif
}

/*
 * Return the minimum order for which x <= (1UL << order).
 * Return -1 if x is 0.
 */
int get_count_order_u32(uint32_t x)
{
	if (!x)
		return -1;

	return fls_u32(x - 1);
}

/*
 * Return the minimum order for which x <= (1UL << order).
 * Return -1 if x is 0.
 */
int get_count_order_u64(uint64_t x)
{
	if (!x)
		return -1;

	return fls_u64(x - 1);
}

/*
 * Return the minimum order for which x <= (1UL << order).
 * Return -1 if x is 0.
 */
int get_count_order_ulong(unsigned long x)
{
	if (!x)
		return -1;

	return fls_ulong(x - 1);
}

const char *get_event_type_str(enum lttng_event_type type)
{
	const char *str_event_type;

	switch (type) {
	case LTTNG_EVENT_ALL:
		str_event_type = str_all;
		break;
	case LTTNG_EVENT_TRACEPOINT:
		str_event_type = str_tracepoint;
		break;
	case LTTNG_EVENT_SYSCALL:
		str_event_type = str_syscall;
		break;
	case LTTNG_EVENT_PROBE:
		str_event_type = str_probe;
		break;
	case LTTNG_EVENT_USERSPACE_PROBE:
		str_event_type = str_userspace_probe;
		break;
	case LTTNG_EVENT_FUNCTION:
		str_event_type = str_function;
		break;
	default:
		/* Should not have an unknown event type or else define it. */
		abort();
	}

	return str_event_type;
}

/*
 * Spawn a lttng relayd daemon by forking and execv.
 */
int spawn_relayd(const char *pathname, int port)
{
	int ret = 0;
	pid_t pid;
	char url[255];

	if (!port) {
		port = DEFAULT_NETWORK_VIEWER_PORT;
	}

	ret = snprintf(url, sizeof(url), "tcp://localhost:%d", port);
	if (ret < 0) {
		goto end;
	}

	MSG("Spawning a relayd daemon");
	pid = fork();
	if (pid == 0) {
		/*
		 * Spawn session daemon and tell
		 * it to signal us when ready.
		 */
		execlp(pathname, "lttng-relayd", "-L", url, NULL);
		/* execlp only returns if error happened */
		if (errno == ENOENT) {
			ERR("No relayd found. Use --relayd-path.");
		} else {
			PERROR("execlp");
		}
		kill(getppid(), SIGTERM); /* wake parent */
		exit(EXIT_FAILURE);
	} else if (pid > 0) {
		goto end;
	} else {
		PERROR("fork");
		ret = -1;
		goto end;
	}

end:
	return ret;
}

/*
 * Check if relayd is alive.
 *
 * Return 1 if found else 0 if NOT found. Negative value on error.
 */
int check_relayd()
{
	int ret, fd;
	struct sockaddr_in sin;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		PERROR("socket check relayd");
		ret = -1;
		goto error_socket;
	}

	sin.sin_family = AF_INET;
	sin.sin_port = htons(DEFAULT_NETWORK_VIEWER_PORT);
	ret = inet_pton(sin.sin_family, "127.0.0.1", &sin.sin_addr);
	if (ret < 1) {
		PERROR("inet_pton check relayd");
		ret = -1;
		goto error;
	}

	/*
	 * A successful connect means the relayd exists thus returning 0 else a
	 * negative value means it does NOT exists.
	 */
	ret = connect(fd, (struct sockaddr *) &sin, sizeof(sin));
	if (ret < 0) {
		/* Not found. */
		ret = 0;
	} else {
		/* Already spawned. */
		ret = 1;
	}

error:
	if (close(fd) < 0) {
		PERROR("close relayd fd");
	}
error_socket:
	return ret;
}

int print_missing_or_multiple_domains(unsigned int domain_count, bool include_agent_domains)
{
	int ret = 0;

	if (domain_count == 0) {
		ERR("Please specify a domain (--kernel/--userspace%s).",
		    include_agent_domains ? "/--jul/--log4j/--log4j2/--python" : "");
		ret = -1;
	} else if (domain_count > 1) {
		ERR("Only one domain must be specified.");
		ret = -1;
	}

	return ret;
}

/*
 * Get the discarded events and lost packet counts.
 */
void print_session_stats(const char *session_name)
{
	char *str;
	const int ret = get_session_stats_str(session_name, &str);

	if (ret >= 0 && str) {
		MSG("%s", str);
		free(str);
	}
}

int get_session_stats_str(const char *session_name, char **out_str)
{
	int count, nb_domains, domain_idx, channel_idx, session_idx, ret;
	struct lttng_domain *domains = nullptr;
	struct lttng_channel *channels = nullptr;
	uint64_t discarded_events_total = 0, lost_packets_total = 0;
	struct lttng_session *sessions = nullptr;
	const struct lttng_session *selected_session = nullptr;
	char *stats_str = nullptr;
	bool print_discarded_events = false, print_lost_packets = false;

	count = lttng_list_sessions(&sessions);
	if (count < 1) {
		ERR("Failed to retrieve session descriptions while printing session statistics.");
		ret = -1;
		goto end;
	}

	/* Identify the currently-selected sessions. */
	for (session_idx = 0; session_idx < count; session_idx++) {
		if (!strcmp(session_name, sessions[session_idx].name)) {
			selected_session = &sessions[session_idx];
			break;
		}
	}
	if (!selected_session) {
		ERR("Failed to retrieve session \"%s\" description while printing session statistics.",
		    session_name);
		ret = -1;
		goto end;
	}

	nb_domains = lttng_list_domains(session_name, &domains);
	if (nb_domains < 0) {
		ret = -1;
		goto end;
	}
	for (domain_idx = 0; domain_idx < nb_domains; domain_idx++) {
		struct lttng_handle *handle =
			lttng_create_handle(session_name, &domains[domain_idx]);

		if (!handle) {
			ERR("Failed to create session handle while printing session statistics.");
			ret = -1;
			goto end;
		}

		free(channels);
		channels = nullptr;
		count = lttng_list_channels(handle, &channels);
		for (channel_idx = 0; channel_idx < count; channel_idx++) {
			uint64_t discarded_events = 0, lost_packets = 0;
			struct lttng_channel *channel = &channels[channel_idx];

			ret = lttng_channel_get_discarded_event_count(channel, &discarded_events);
			if (ret) {
				ERR("Failed to retrieve discarded event count from channel %s",
				    channel->name);
			}

			ret = lttng_channel_get_lost_packet_count(channel, &lost_packets);
			if (ret) {
				ERR("Failed to retrieve lost packet count from channel %s",
				    channel->name);
			}

			discarded_events_total += discarded_events;
			lost_packets_total += lost_packets;
		}
		lttng_destroy_handle(handle);
	}

	print_discarded_events = discarded_events_total > 0 && !selected_session->snapshot_mode;
	print_lost_packets = lost_packets_total > 0 && !selected_session->snapshot_mode;

	if (print_discarded_events && print_lost_packets) {
		ret = asprintf(&stats_str,
			       "Warning: %" PRIu64 " events were discarded and %" PRIu64
			       " packets were lost, please refer to "
			       "the documentation on channel configuration.",
			       discarded_events_total,
			       lost_packets_total);
	} else if (print_discarded_events) {
		ret = asprintf(&stats_str,
			       "Warning: %" PRIu64 " events were discarded, please refer to "
			       "the documentation on channel configuration.",
			       discarded_events_total);
	} else if (print_lost_packets) {
		ret = asprintf(&stats_str,
			       "Warning: %" PRIu64 " packets were lost, please refer to "
			       "the documentation on channel configuration.",
			       lost_packets_total);
	} else {
		ret = 0;
	}

	if (ret < 0) {
		ERR("Failed to format lost packet and discarded events statistics");
	} else {
		*out_str = stats_str;
		ret = 0;
	}
end:
	free(sessions);
	free(channels);
	free(domains);
	return ret;
}

int show_cmd_help(const char *cmd_name, const char *help_msg)
{
	int ret;
	char page_name[32];

	ret = sprintf(page_name, "lttng-%s", cmd_name);
	LTTNG_ASSERT(ret > 0 && ret < 32);
	ret = utils_show_help(1, page_name, help_msg);
	if (ret && !help_msg) {
		ERR("Cannot view man page `lttng-%s(1)`", cmd_name);
		perror("exec");
	}

	return ret;
}

int print_trace_archive_location(const struct lttng_trace_archive_location *location,
				 const char *session_name)
{
	int ret = 0;
	enum lttng_trace_archive_location_type location_type;
	enum lttng_trace_archive_location_status status;
	bool printed_location = false;

	location_type = lttng_trace_archive_location_get_type(location);

	_MSG("Trace chunk archive for session %s is now readable", session_name);
	switch (location_type) {
	case LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_LOCAL:
	{
		const char *absolute_path;

		status = lttng_trace_archive_location_local_get_absolute_path(location,
									      &absolute_path);
		if (status != LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK) {
			ret = -1;
			goto end;
		}
		MSG(" at %s", absolute_path);
		printed_location = true;
		break;
	}
	case LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_RELAY:
	{
		uint16_t control_port, data_port;
		const char *host, *relative_path, *protocol_str;
		enum lttng_trace_archive_location_relay_protocol_type protocol;

		/* Fetch all relay location parameters. */
		status = lttng_trace_archive_location_relay_get_protocol_type(location, &protocol);
		if (status != LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK) {
			ret = -1;
			goto end;
		}

		status = lttng_trace_archive_location_relay_get_host(location, &host);
		if (status != LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK) {
			ret = -1;
			goto end;
		}

		status = lttng_trace_archive_location_relay_get_control_port(location,
									     &control_port);
		if (status != LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK) {
			ret = -1;
			goto end;
		}

		status = lttng_trace_archive_location_relay_get_data_port(location, &data_port);
		if (status != LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK) {
			ret = -1;
			goto end;
		}

		status = lttng_trace_archive_location_relay_get_relative_path(location,
									      &relative_path);
		if (status != LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK) {
			ret = -1;
			goto end;
		}

		switch (protocol) {
		case LTTNG_TRACE_ARCHIVE_LOCATION_RELAY_PROTOCOL_TYPE_TCP:
			protocol_str = "tcp";
			break;
		default:
			protocol_str = "unknown";
			break;
		}

		MSG(" on relay %s://%s/%s [control port %" PRIu16 ", data port %" PRIu16 "]",
		    protocol_str,
		    host,
		    relative_path,
		    control_port,
		    data_port);
		printed_location = true;
		break;
	}
	default:
		break;
	}
end:
	if (!printed_location) {
		MSG(" at an unknown location");
	}
	return ret;
}

namespace {
template <typename FilterFunctionType>
lttng::cli::session_list get_sessions(const FilterFunctionType& filter,
				      bool return_first_match_only = false)
{
	lttng::cli::session_list list = []() {
		int list_ret;
		struct lttng_session *psessions;

		list_ret = lttng_list_sessions(&psessions);

		if (list_ret < 0) {
			LTTNG_THROW_CTL("Failed to list sessions",
					static_cast<lttng_error_code>(list_ret));
		}

		return lttng::cli::session_list(psessions, list_ret);
	}();

	std::size_t write_to = 0;
	for (std::size_t read_from = 0; read_from < list.size(); ++read_from) {
		if (!filter(list[read_from])) {
			continue;
		}

		if (read_from != write_to) {
			list[write_to] = list[read_from];
		}

		++write_to;

		if (return_first_match_only) {
			return lttng::cli::session_list(std::move(list), 1);
		}
	}

	list.resize(write_to);

	return list;
}
} /* namespace */

lttng::cli::session_list lttng::cli::list_sessions(const struct session_spec& spec)
{
	switch (spec.type_) {
	case lttng::cli::session_spec::type::NAME:
		if (spec.value == nullptr) {
			const auto configured_name =
				lttng::make_unique_wrapper<char, lttng::memory::free>(
					get_session_name());

			if (!configured_name) {
				LTTNG_THROW_CLI_NO_DEFAULT_SESSION();
			}

			const struct lttng::cli::session_spec new_spec(
				lttng::cli::session_spec::type::NAME, configured_name.get());

			return list_sessions(new_spec);
		}

		return get_sessions(
			[&spec](const lttng_session& session) {
				return strcmp(session.name, spec.value) == 0;
			},
			true);
	case lttng::cli::session_spec::type::GLOB_PATTERN:
		return get_sessions([&spec](const lttng_session& session) {
			return fnmatch(spec.value, session.name, 0) == 0;
		});
	case lttng::cli::session_spec::type::ALL:
		return get_sessions([](const lttng_session&) { return true; });
	}

	return lttng::cli::session_list();
}

void print_kernel_tracer_status_error()
{
	if (lttng_opt_mi) {
		return;
	}

	enum lttng_kernel_tracer_status kernel_tracer_status;
	const auto ret = lttng_get_kernel_tracer_status(&kernel_tracer_status);

	if (ret < 0) {
		ERR("Failed to get kernel tracer status: %s", lttng_strerror(ret));
	} else {
		switch (kernel_tracer_status) {
		case LTTNG_KERNEL_TRACER_STATUS_INITIALIZED:
			return;
		case LTTNG_KERNEL_TRACER_STATUS_ERR_MODULES_UNKNOWN:
			std::cerr << "\tKernel module loading failed" << '\n';
			break;
		case LTTNG_KERNEL_TRACER_STATUS_ERR_MODULES_MISSING:
			std::cerr << "\tMissing one or more required kernel modules" << '\n';
			break;
		case LTTNG_KERNEL_TRACER_STATUS_ERR_MODULES_SIGNATURE:
			std::cerr
				<< "\tKernel module signature error prevented loading of one or more required kernel modules"
				<< '\n';
			break;
		case LTTNG_KERNEL_TRACER_STATUS_ERR_NEED_ROOT:
			std::cerr << "\tlttng-sessiond isn't running as root" << '\n';
			break;
		case LTTNG_KERNEL_TRACER_STATUS_ERR_NOTIFIER:
			std::cerr << "\tFailed to setup notifiers" << '\n';
			break;
		case LTTNG_KERNEL_TRACER_STATUS_ERR_OPEN_PROC_LTTNG:
			std::cerr << "\tlttng-sessiond failed to open /proc/lttng" << '\n';
			break;
		case LTTNG_KERNEL_TRACER_STATUS_ERR_VERSION_MISMATCH:
			std::cerr << "\tVersion mismatch between kernel tracer and kernel tracer ABI"
				  << '\n';
			break;
		default:
			std::cerr << lttng::format("\t\tUnknown kernel tracer status (%d)",
						   static_cast<int>(kernel_tracer_status))
				  << '\n';
			break;
		}

		std::cerr << "\tConsult lttng-sessiond logs for more information" << '\n';
	}
}
