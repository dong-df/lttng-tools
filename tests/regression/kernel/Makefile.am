# SPDX-License-Identifier: GPL-2.0-only


include $(top_srcdir)/aminclude_static.am

EXTRA_DIST = test_all_events \
						 test_callstack \
						 test_channel \
						 test_clock_override \
						 test_event_basic \
						 test_high_throughput.py \
						 test_kernel_function \
						 test_lttng_logger \
						 test_ns_contexts \
						 test_ns_contexts_change \
						 test_rotation_destroy_flush \
						 test_select_poll_epoll \
						 test_syscall \
						 test_userspace_probe \
						 validate_select_poll_epoll.py

noinst_PROGRAMS = select_poll_epoll
select_poll_epoll_SOURCES = select_poll_epoll.cpp
select_poll_epoll_LDADD = $(POPT_LIBS) $(top_builddir)/src/common/libcommon-lgpl.la
select_poll_epoll_CPPFLAGS = $(POPT_CFLAGS) $(AM_CPPFLAGS)
# Disable stack protection, the test overflows the stack to test the poll/epoll syscalls
select_poll_epoll_CXXFLAGS = -fno-stack-protector $(AM_CXXFLAGS)

all-local:
	@if [ x"$(srcdir)" != x"$(builddir)" ]; then \
		for script in $(EXTRA_DIST); do \
			cp -f $(srcdir)/$$script $(builddir); \
		done; \
	fi

clean-local:
	@if [ x"$(srcdir)" != x"$(builddir)" ]; then \
		for script in $(EXTRA_DIST); do \
			rm -f $(builddir)/$$script; \
		done; \
	fi
