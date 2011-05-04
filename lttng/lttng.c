/*
 * Copyright (c)  2011 David Goulet <david.goulet@polymtl.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <lttng/lttng.h>

#include "lttng.h"
#include "lttngerr.h"

/* Variables */
static char *progname;
static char short_uuid[9];

/* Prototypes */
static int process_client_opt(void);
static int process_opt_list_apps(void);
static int process_opt_list_sessions(void);
static int process_opt_list_traces(void);
static int process_opt_create_session(void);
static int process_opt_session_uuid(void);
static void sighandler(int sig);
static void shorten_uuid(char *in, char *out);
static int set_signal_handler(void);
static int validate_options(void);
static char *get_cmdline_by_pid(pid_t pid);

/*
 *  start_client
 *
 *  Process client request from the command line
 *  options. Every tracing action is done by the
 *  liblttngctl API.
 */
static int process_client_opt(void)
{
	int ret;
	uuid_t uuid;

	if (opt_list_apps) {
		ret = process_opt_list_apps();
		if (ret < 0) {
			goto end;
		}
	}

	if (opt_list_session) {
		ret = process_opt_list_sessions();
		if (ret < 0) {
			goto end;
		}
	}

	if (opt_list_traces) {
		ret = process_opt_list_traces();
		if (ret < 0) {
			goto end;
		}
	}

	if (opt_create_session != NULL) {
		ret = process_opt_create_session();
		if (ret < 0) {
			goto end;
		}
	}

	if (opt_destroy_session != NULL) {
		uuid_parse(opt_destroy_session, uuid);
		ret = lttng_destroy_session(&uuid);
		if (ret < 0) {
			goto end;
		}
	}

	if (opt_session_uuid != NULL) {
		DBG("Set session uuid to %s", short_uuid);
		ret = process_opt_session_uuid();
		if (ret < 0) {
			ERR("Session UUID %s not found", opt_session_uuid);
			goto error;
		}
	}

	if (opt_trace_kernel) {
		ERR("Not implemented yet");
		goto end;
	}

	if (opt_trace_pid != 0) {
		if (opt_create_trace) {
			DBG("Create a userspace trace for pid %d", opt_trace_pid);
			ret = lttng_ust_create_trace(opt_trace_pid);
			if (ret < 0) {
				goto end;
			}
			MSG("Trace created successfully!\nUse --start to start tracing.");
		}

		if (opt_start_trace) {
			DBG("Start trace for pid %d", opt_trace_pid);
			ret = lttng_ust_start_trace(opt_trace_pid);
			if (ret < 0) {
				goto end;
			}
			MSG("Trace started successfully!");
		} else if (opt_stop_trace) {
			DBG("Stop trace for pid %d", opt_trace_pid);
			ret = lttng_ust_stop_trace(opt_trace_pid);
			if (ret < 0) {
				goto end;
			}
			MSG("Trace stopped successfully!");
		}

	}

	return 0;

end:
	ERR("%s", lttng_get_readable_code(ret));
	return ret;

error:
	return ret;
}

/*
 *  process_opt_session_uuid
 *
 *  Set current session uuid to the current flow of
 *  command(s) using the already shorten uuid.
 */
static int process_opt_session_uuid(void)
{
	int ret, count, i;
	struct lttng_session *sessions;

	count = lttng_list_sessions(&sessions);
	if (count < 0) {
		ret = count;
		goto error;
	}

	for (i = 0; i < count; i++) {
		if (strncmp(sessions[i].uuid, short_uuid, 8) == 0) {
			lttng_set_current_session_uuid(sessions[i].uuid);
			break;
		}
	}

	free(sessions);

	return 0;

error:
	return ret;
}

/*
 *  process_opt_list_traces
 *
 *  Get list of all traces for a specific session uuid.
 */
static int process_opt_list_traces(void)
{
	int ret, i;
	uuid_t uuid;
	struct lttng_trace *traces;

	uuid_parse(opt_session_uuid, uuid);
	ret = lttng_list_traces(&uuid, &traces);
	if (ret < 0) {
		goto error;
	}

	MSG("Userspace traces:");
	for (i = 0; i < ret; i++) {
		if (traces[i].type == USERSPACE) {
			MSG("\t%d) %s (pid: %d): %s",
					i, traces[i].name, traces[i].pid,
					get_cmdline_by_pid(traces[i].pid));
		} else {
			break;
		}
	}

	MSG("Kernel traces:");
	for (;i < ret; i++) {
		if (traces[i].type == KERNEL) {
			MSG("\t%d) %s", i, traces[i].name);
		}
	}

	free(traces);

error:
	return ret;
}

/*
 *  process_opt_create_session
 *
 *  Create a new session using the name pass
 *  to the command line.
 */
static int process_opt_create_session(void)
{
	int ret;
	uuid_t session_id;
	char str_uuid[37];

	ret = lttng_create_session(opt_create_session, &session_id);
	if (ret < 0) {
		goto error;
	}

	uuid_unparse(session_id, str_uuid);

	MSG("Session created:");
	MSG("    %s (%s)", opt_create_session, str_uuid);

error:
	return ret;
}

/*
 *  extract_short_uuid
 *
 *  Extract shorten uuid and copy it to out.
 *  Shorten uuid format : '<name>.<short_uuid>'
 */
static int extract_short_uuid(char *in, char *out)
{
	char *tok;

	tok = strchr(in, '.');
	if (strlen(tok+1) == 8) {
		memcpy(out, tok+1, 8);
		out[9] = '\0';
		return 0;
	}

	return -1;
}

/*
 * shorten_uuid
 *
 * Small function to shorten the 37 bytes long uuid_t
 * string representation to 8 characters.
 */
static void shorten_uuid(char *in, char *out)
{
	memcpy(out, in, 8);
	out[8] = '\0';
}

/*
 *  process_opt_list_sessions
 *
 *  Get the list of available sessions from
 *  the session daemon and print it to user.
 */
static int process_opt_list_sessions(void)
{
	int ret, count, i;
	char tmp_short_uuid[9];
	struct lttng_session *sess;

	count = lttng_list_sessions(&sess);
	DBG("Session count %d", count);
	if (count < 0) {
		ret = count;
		goto error;
	}

	MSG("Available sessions (UUIDs):");
	for (i = 0; i < count; i++) {
		shorten_uuid(sess[i].uuid, tmp_short_uuid);
		MSG("    %d) %s.%s", i+1, sess[i].name, tmp_short_uuid);
	}

	free(sess);
	MSG("\nTo select a session, use -s, --session UUID.");

	return 0;

error:
	return ret;
}

/*
 *  process_opt_list_apps
 *
 *  Get the UST traceable pid list and print
 *  them to the user.
 */
static int process_opt_list_apps(void)
{
	int i, ret, count;
	pid_t *pids;
	char *cmdline;

	count = lttng_ust_list_apps(&pids);
	if (count < 0) {
		ret = count;
		goto error;
	}

	MSG("LTTng UST traceable application [name (pid)]:");
	for (i=0; i < count; i++) {
		cmdline = get_cmdline_by_pid(pids[i]);
		if (cmdline == NULL) {
			MSG("\t(not running) (%d)", pids[i]);
			continue;
		}
		MSG("\t%s (%d)", cmdline, pids[i]);
		free(cmdline);
	}

	/* Allocated by lttng_ust_list_apps() */
	free(pids);

	return 0;

error:
	return ret;
}

/*
 *  get_cmdline_by_pid
 *
 *  Get command line from /proc for a specific pid.
 *
 *  On success, return an allocated string pointer pointing to
 *  the proc cmdline.
 *  On error, return NULL.
 */
static char *get_cmdline_by_pid(pid_t pid)
{
	int ret;
	FILE *fp;
	char *cmdline = NULL;
	char path[24];	/* Can't go bigger than /proc/65535/cmdline */

	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	fp = fopen(path, "r");
	if (fp == NULL) {
		goto end;
	}

	/* Caller must free() *cmdline */
	cmdline = malloc(PATH_MAX);
	ret = fread(cmdline, 1, PATH_MAX, fp);
	fclose(fp);

end:
	return cmdline;
}

/*
 *  validate_options
 *
 *  Make sure that all options passed to the command line
 *  are compatible with each others.
 *
 *  On error, return -1
 *  On success, return 0
 */
static int validate_options(void)
{
	int ret;

	/* Conflicting command */
	if (opt_start_trace && opt_stop_trace) {
		ERR("Can't use --start and --stop together.");
		goto error;
	/* Must have a session UUID for trace action. */
	} else if ((opt_session_uuid == NULL) &&
			(opt_create_trace || opt_start_trace || opt_stop_trace || opt_list_traces)) {
		ERR("You need to specify a session UUID.\nPlease use --session UUID to do so.");
		goto error;
	/* If no PID specified and trace_kernel is off */
	} else if ((opt_trace_pid == 0 && opt_trace_kernel == 0) &&
			(opt_create_trace || opt_start_trace || opt_stop_trace)) {
		ERR("Please specify a PID using -p, --pid PID.");
		goto error;
	}

	if (opt_session_uuid != NULL) {
		ret = extract_short_uuid(opt_session_uuid, short_uuid);
		if (ret < 0) {
			ERR("Session UUID not valid. Must be <name>.<short_uuid>");
			goto error;
		}
	}

	return 0;

error:
	return -1;
}

/*
 *  spawn_sessiond
 *
 *  Spawn a session daemon by forking and execv.
 */
static int spawn_sessiond(char *pathname)
{
	int ret = 0;
	pid_t pid;

	MSG("Spawning session daemon");
	pid = fork();
	if (pid == 0) {
		/* Spawn session daemon and tell
		 * it to signal us when ready.
		 */
		ret = execlp(pathname, "ltt-sessiond", "--sig-parent", "--quiet", NULL);
		if (ret < 0) {
			if (errno == ENOENT) {
				ERR("No session daemon found. Use --sessiond-path.");
			} else {
				perror("execlp");
			}
			kill(getppid(), SIGTERM);
			exit(EXIT_FAILURE);
		}
		exit(EXIT_SUCCESS);
	} else if (pid > 0) {
		/* Wait for ltt-sessiond to start */
		pause();
		goto end;
	} else {
		perror("fork");
		ret = -1;
		goto end;
	}

end:
	return ret;
}

/*
 *  check_ltt_sessiond
 *
 *  Check if the session daemon is available using
 *  the liblttngctl API for the check. If not, try to
 *  spawn a daemon.
 */
static int check_ltt_sessiond(void)
{
	int ret;
	char *pathname = NULL;

	ret = lttng_check_session_daemon();
	if (ret < 0) {
		/* Try command line option path */
		if (opt_sessiond_path != NULL) {
			ret = access(opt_sessiond_path, F_OK | X_OK);
			if (ret < 0) {
				ERR("No such file: %s", opt_sessiond_path);
				goto end;
			}
			pathname = opt_sessiond_path;
		} else {
			/* Try LTTNG_SESSIOND_PATH env variable */
			pathname = getenv(LTTNG_SESSIOND_PATH_ENV);
			if (pathname != NULL) {
				/* strdup here in order to make the free()
				 * not fail later on.
				 */
				pathname = strdup(pathname);
			}
		}

		/* Let's rock and roll */
		if (pathname == NULL) {
			ret = asprintf(&pathname, "ltt-sessiond");
			if (ret < 0) {
				goto end;
			}
		}

		ret = spawn_sessiond(pathname);
		free(pathname);
		if (ret < 0) {
			ERR("Problem occurs when starting %s", pathname);
			goto end;
		}
	}

end:
	return ret;
}

/*
 *  set_signal_handler
 *
 *  Setup signal handler for SIGCHLD and SIGTERM.
 */
static int set_signal_handler(void)
{
	int ret = 0;
	struct sigaction sa;
	sigset_t sigset;

	if ((ret = sigemptyset(&sigset)) < 0) {
		perror("sigemptyset");
		goto end;
	}

	sa.sa_handler = sighandler;
	sa.sa_mask = sigset;
	sa.sa_flags = 0;
	if ((ret = sigaction(SIGCHLD, &sa, NULL)) < 0) {
		perror("sigaction");
		goto end;
	}

	if ((ret = sigaction(SIGTERM, &sa, NULL)) < 0) {
		perror("sigaction");
		goto end;
	}

end:
	return ret;
}

/*
 *  sighandler
 *
 *  Signal handler for the daemon
 */
static void sighandler(int sig)
{
	switch (sig) {
		case SIGTERM:
			DBG("SIGTERM catched");
			clean_exit(EXIT_FAILURE);
			break;
		case SIGCHLD:
			/* Notify is done */
			DBG("SIGCHLD catched");
			break;
		default:
			DBG("Unknown signal %d catched", sig);
			break;
	}

	return;
}

/*
 * clean_exit
 */
void clean_exit(int code)
{
	DBG("Clean exit");
	exit(code);
}

/*
 *  main
 */
int main(int argc, char *argv[])
{
	int ret;

	progname = argv[0] ? argv[0] : "lttng";

	/* For Mathieu Desnoyers aka Dr Tracing */
	if (strncmp(progname, "drtrace", 7) == 0) {
		MSG("%c[%d;%dmWelcome back Dr Tracing!%c[%dm\n\n", 27,1,33,27,0);
	}

	ret = parse_args(argc, (const char **) argv);
	if (ret < 0) {
		clean_exit(EXIT_FAILURE);
	}

	ret = validate_options();
	if (ret < 0) {
		return EXIT_FAILURE;
	}

	ret = set_signal_handler();
	if (ret < 0) {
		clean_exit(ret);
	}

	if (opt_tracing_group != NULL) {
		DBG("Set tracing group to '%s'", opt_tracing_group);
		lttng_set_tracing_group(opt_tracing_group);
	}

	/* If ask for kernel tracing, need root perms */
	if (opt_trace_kernel) {
		DBG("Kernel tracing activated");
		if (getuid() != 0) {
			ERR("%s must be setuid root", progname);
			clean_exit(-EPERM);
		}
	}

	/* Check if the lttng session daemon is running.
	 * If no, a daemon will be spawned.
	 */
	if (opt_no_sessiond == 0 && (check_ltt_sessiond() < 0)) {
		clean_exit(EXIT_FAILURE);
	}

	ret = process_client_opt();
	if (ret < 0) {
		clean_exit(ret);
	}

	clean_exit(0);

	return 0;
}
