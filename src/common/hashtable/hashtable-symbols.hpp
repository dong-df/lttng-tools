#ifndef _HASHTABLE_SYMBOLS_H
#define _HASHTABLE_SYMBOLS_H

/*
 * hashtable-symbols.h
 *
 * LTTng hash table symbol prefixing
 *
 * SPDX-FileCopyrightText: 2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#define _cds_lfht_new		       lttng__cds_lfht_new
#define cds_lfht_add		       lttng_cds_lfht_add
#define cds_lfht_add_replace	       lttng_cds_lfht_add_replace
#define cds_lfht_add_unique	       lttng_cds_lfht_add_unique
#define cds_lfht_count_nodes	       lttng_cds_lfht_count_nodes
#define cds_lfht_del		       lttng_cds_lfht_del
#define cds_lfht_destroy	       lttng_cds_lfht_destroy
#define cds_lfht_first		       lttng_cds_lfht_first
#define cds_lfht_fls_ulong	       lttng_cds_lfht_fls_ulong
#define cds_lfht_get_count_order_u32   lttng_cds_lfht_get_count_order_u32
#define cds_lfht_get_count_order_ulong lttng_cds_lfht_get_count_order_ulong
#define cds_lfht_is_node_deleted       lttng_cds_lfht_is_node_deleted
#define cds_lfht_lookup		       lttng_cds_lfht_lookup
#define cds_lfht_next		       lttng_cds_lfht_next
#define cds_lfht_next_duplicate	       lttng_cds_lfht_next_duplicate
#define cds_lfht_replace	       lttng_cds_lfht_replace
#define cds_lfht_resize		       lttng_cds_lfht_resize

#endif /* _HASHTABLE_SYMBOLS_H */
