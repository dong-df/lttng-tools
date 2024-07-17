/*
 * Copyright (C) 2022 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef LTTNG_URCU_H
#define LTTNG_URCU_H

#define _LGPL_SOURCE
#include <common/exception.hpp>
#include <common/hashtable/hashtable.hpp>
#include <common/macros.hpp>

#include <iterator>
#include <mutex>
#include <urcu.h>
#include <urcu/rculfhash.h>

namespace lttng {
namespace urcu {

namespace details {
/*
 * Wrapper around an urcu read lock which satisfies the 'Mutex' named
 * requirements of C++11. Satisfying those requirements facilitates the use of
 * standard concurrency support library facilities.
 *
 * read_lock is under the details namespace since it is unlikely to be used
 * directly by exception-safe code. See read_lock_guard.
 */
class read_lock {
public:
	read_lock() = default;
	~read_lock() = default;

	/* "Not copyable" and "not moveable" Mutex requirements. */
	read_lock(const read_lock&) = delete;
	read_lock(read_lock&&) = delete;
	read_lock& operator=(read_lock&&) = delete;
	read_lock& operator=(const read_lock&) = delete;

	void lock()
	{
		rcu_read_lock();
	}

	bool try_lock()
	{
		lock();
		return true;
	}

	void unlock()
	{
		rcu_read_unlock();
	}
};
} /* namespace details */

/*
 * Provides the basic concept of std::lock_guard for rcu reader locks.
 *
 * The RCU reader lock is held for the duration of lock_guard's lifetime.
 */
class read_lock_guard {
public:
	read_lock_guard() = default;
	~read_lock_guard() = default;

	read_lock_guard(const read_lock_guard&) = delete;
	read_lock_guard(read_lock_guard&&) = delete;
	read_lock_guard& operator=(read_lock_guard&&) = delete;
	read_lock_guard& operator=(const read_lock_guard&) = delete;

private:
	details::read_lock _lock;
	std::lock_guard<details::read_lock> _guard{ _lock };
};

using unique_read_lock = std::unique_lock<details::read_lock>;

namespace details {

/* Implementation for types that contain a straight cds_lfht_node. */
template <typename ContainedType, typename NodeType, NodeType ContainedType::*Member>
static typename std::enable_if<std::is_same<cds_lfht_node, NodeType>::value, ContainedType *>::type
get_element_from_node(cds_lfht_node& node)
{
	return lttng::utils::container_of(&node, Member);
}

/* Specialization for NodeType deriving from lttng_ht_node. */
template <typename ContainedType, typename NodeType, NodeType ContainedType::*Member>
static typename std::enable_if<std::is_base_of<lttng_ht_node, NodeType>::value, ContainedType *>::type
get_element_from_node(cds_lfht_node& node)
{
	return lttng_ht_node_container_of(&node, Member);
}
} /* namespace details */

/*
 * The lfht_iteration_adapter class template wraps the liburcu lfht API to provide iteration
 * capabilities. It allows users to iterate over a lock-free hash table with ranged-for semantics
 * while holding the RCU read lock. The reader lock is held for the lifetime of the iteration
 * adapter (i.e. not the lifetime of the iterators it provides).
 */
template <typename ContainedType, typename NodeType, NodeType ContainedType::*Member>
class lfht_iteration_adapter {
public:
	/* Nested iterator class defines the iterator for lfht_iteration_adapter. */
	class iterator : public std::iterator<std::input_iterator_tag, std::uint64_t> {
		/* Allow lfht_iteration_adapter to access private members of iterator_base. */
		friend lfht_iteration_adapter;

	public:
		iterator(const iterator& other) = default;
		iterator(iterator&& other) noexcept = default;
		~iterator() = default;

		/* Move to the next element in the hash table. */
		iterator& operator++()
		{
			cds_lfht_next(&_ht, &_it);
			return *this;
		}

		bool operator==(const iterator& other) const noexcept
		{
			/* Compare pointed nodes by address. */
			return other._it.node == _it.node;
		}

		bool operator!=(const iterator& other) const noexcept
		{
			return !(*this == other);
		}

		/* Dereference the iterator to access the contained element. */
		ContainedType *operator*() const
		{
			auto *node = _it.node;

			/* Throw an exception if dereferencing an end iterator. */
			if (!node) {
				LTTNG_THROW_OUT_OF_RANGE(
					"Dereferenced an iterator at the end of a liburcu hashtable");
			}

			/* Retrieve the element from the node. */
			return details::get_element_from_node<ContainedType, NodeType, Member>(
				*node);
		}

	private:
		iterator(cds_lfht& ht, const cds_lfht_iter& it) : _ht(ht), _it(it)
		{
		}

		/* Reference to the hash table being iterated over. */
		cds_lfht& _ht;
		/* Native lfht iterator pointing to the current position. */
		cds_lfht_iter _it;
	};

	explicit lfht_iteration_adapter(cds_lfht& ht) : _ht(ht)
	{
	}

	/* Return an iterator to the beginning of the hash table. */
	iterator begin() const noexcept
	{
		cds_lfht_iter it;

		cds_lfht_first(&_ht, &it);
		return iterator(_ht, it);
	}

	/* Return an iterator to the end of the hash table. */
	iterator end() const noexcept
	{
		const cds_lfht_iter it = {};

		return iterator(_ht, it);
	}

private:
	/* Reference to the hash table being iterated over. */
	cds_lfht& _ht;
	/* RCU read lock held during the iteration. */
	const lttng::urcu::read_lock_guard read_lock;
};

} /* namespace urcu */
} /* namespace lttng */

#endif /* LTTNG_URCU_H */
