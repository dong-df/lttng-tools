/*
 * SPDX-FileCopyrightText: 2006 Bob Jenkins
 * SPDX-FileCopyrightText: 2011 EfficiOS Inc.
 * SPDX-FileCopyrightText: 2011 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

/*
 * These are functions for producing 32-bit hashes for hash table lookup.
 * hashword(), hashlittle(), hashlittle2(), hashbig(), mix(), and final() are
 * externally useful functions.  Routines to test the hash are included if
 * SELF_TEST is defined.  You can use this free for any purpose.  It's in the
 * public domain.  It has no warranty.
 *
 * You probably want to use hashlittle().  hashlittle() and hashbig() hash byte
 * arrays.  hashlittle() is is faster than hashbig() on little-endian machines.
 * Intel and AMD are little-endian machines.  On second thought, you probably
 * want hashlittle2(), which is identical to hashlittle() except it returns two
 * 32-bit hashes for the price of one.  You could implement hashbig2() if you
 * wanted but I haven't bothered here.
 *
 * If you want to find a hash of, say, exactly 7 integers, do
 *   a = i1;  b = i2;  c = i3;
 *   mix(a,b,c);
 *   a += i4; b += i5; c += i6;
 *   mix(a,b,c);
 *   a += i7;
 *   final(a,b,c);
 * then use c as the hash value.  If you have a variable length array of
 * 4-byte integers to hash, use hashword().  If you have a byte array (like
 * a character string), use hashlittle().  If you have several byte arrays, or
 * a mix of things, see the comments above hashlittle().
 *
 * Why is this so big?  I read 12 bytes at a time into 3 4-byte integers, then
 * mix those integers.  This is fast (you can do a lot more thorough mixing
 * with 12*3 instructions on 3 integers than you can with 3 instructions on 1
 * byte), but shoehorning those bytes into integers efficiently is messy.
 */

#define _LGPL_SOURCE
#include "utils.hpp"

#include <common/common.hpp>
#include <common/compat/endian.hpp> /* attempt to define endianness */
#include <common/hashtable/hashtable.hpp>

#include <stdint.h> /* defines uint32_t etc */
#include <stdio.h> /* defines printf for tests */
#include <string.h>
#include <sys/param.h> /* attempt to define endianness */
#include <time.h> /* defines time_t for timings in the test */
#include <urcu/compiler.h>

/*
 * My best guess at if you are big-endian or little-endian.  This may
 * need adjustment.
 */
#if (defined(BYTE_ORDER) && defined(LITTLE_ENDIAN) && BYTE_ORDER == LITTLE_ENDIAN) ||    \
	(defined(i386) || defined(__i386__) || defined(__i486__) || defined(__i586__) || \
	 defined(__i686__) || defined(vax) || defined(MIPSEL))
#define HASH_LITTLE_ENDIAN 1
#define HASH_BIG_ENDIAN	   0
#elif (defined(BYTE_ORDER) && defined(BIG_ENDIAN) && BYTE_ORDER == BIG_ENDIAN) || \
	(defined(sparc) || defined(POWERPC) || defined(mc68000) || defined(sel))
#define HASH_LITTLE_ENDIAN 0
#define HASH_BIG_ENDIAN	   1
#else
#define HASH_LITTLE_ENDIAN 0
#define HASH_BIG_ENDIAN	   0
#endif

#define hashsize(n) ((uint32_t) 1 << (n))
#define hashmask(n) (hashsize(n) - 1)
#define rot(x, k)   (((x) << (k)) | ((x) >> (32 - (k))))

/*
 * mix -- mix 3 32-bit values reversibly.
 *
 * This is reversible, so any information in (a,b,c) before mix() is
 * still in (a,b,c) after mix().
 *
 * If four pairs of (a,b,c) inputs are run through mix(), or through
 * mix() in reverse, there are at least 32 bits of the output that
 * are sometimes the same for one pair and different for another pair.
 * This was tested for:
 * * pairs that differed by one bit, by two bits, in any combination
 *   of top bits of (a,b,c), or in any combination of bottom bits of
 *   (a,b,c).
 * * "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
 *   the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
 *   is commonly produced by subtraction) look like a single 1-bit
 *   difference.
 * * the base values were pseudorandom, all zero but one bit set, or
 *   all zero plus a counter that starts at zero.
 *
 * Some k values for my "a-=c; a^=rot(c,k); c+=b;" arrangement that
 * satisfy this are
 *     4  6  8 16 19  4
 *     9 15  3 18 27 15
 *    14  9  3  7 17  3
 * Well, "9 15 3 18 27 15" didn't quite get 32 bits diffing
 * for "differ" defined as + with a one-bit base and a two-bit delta.  I
 * used http://burtleburtle.net/bob/hash/avalanche.html to choose
 * the operations, constants, and arrangements of the variables.
 *
 * This does not achieve avalanche.  There are input bits of (a,b,c)
 * that fail to affect some output bits of (a,b,c), especially of a.  The
 * most thoroughly mixed value is c, but it doesn't really even achieve
 * avalanche in c.
 *
 * This allows some parallelism.  Read-after-writes are good at doubling
 * the number of bits affected, so the goal of mixing pulls in the opposite
 * direction as the goal of parallelism.  I did what I could.  Rotates
 * seem to cost as much as shifts on every machine I could lay my hands
 * on, and rotates are much kinder to the top and bottom bits, so I used
 * rotates.
 */
#define mix(a, b, c)               \
	{                          \
		(a) -= (c);        \
		(a) ^= rot(c, 4);  \
		(c) += (b);        \
		(b) -= (a);        \
		(b) ^= rot(a, 6);  \
		(a) += (c);        \
		(c) -= (b);        \
		(c) ^= rot(b, 8);  \
		(b) += (a);        \
		(a) -= (c);        \
		(a) ^= rot(c, 16); \
		(c) += (b);        \
		(b) -= (a);        \
		(b) ^= rot(a, 19); \
		(a) += (c);        \
		(c) -= (b);        \
		(c) ^= rot(b, 4);  \
		(b) += (a);        \
	}

/*
 * final -- final mixing of 3 32-bit values (a,b,c) into c
 *
 * Pairs of (a,b,c) values differing in only a few bits will usually
 * produce values of c that look totally different.  This was tested for
 * * pairs that differed by one bit, by two bits, in any combination
 *   of top bits of (a,b,c), or in any combination of bottom bits of
 *   (a,b,c).
 * * "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
 *   the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
 *   is commonly produced by subtraction) look like a single 1-bit
 *   difference.
 * * the base values were pseudorandom, all zero but one bit set, or
 *   all zero plus a counter that starts at zero.
 *
 * These constants passed:
 *  14 11 25 16 4 14 24
 *  12 14 25 16 4 14 24
 * and these came close:
 *   4  8 15 26 3 22 24
 *  10  8 15 26 3 22 24
 *  11  8 15 26 3 22 24
 */
#define final(a, b, c)             \
	{                          \
		(c) ^= (b);        \
		(c) -= rot(b, 14); \
		(a) ^= (c);        \
		(a) -= rot(c, 11); \
		(b) ^= (a);        \
		(b) -= rot(a, 25); \
		(c) ^= (b);        \
		(c) -= rot(b, 16); \
		(a) ^= (c);        \
		(a) -= rot(c, 4);  \
		(b) ^= (a);        \
		(b) -= rot(a, 14); \
		(c) ^= (b);        \
		(c) -= rot(b, 24); \
	}

/*
 * k - the key, an array of uint32_t values
 * length - the length of the key, in uint32_ts
 * initval - the previous hash, or an arbitrary value
 */
static uint32_t __attribute__((unused)) hashword(const uint32_t *k, size_t length, uint32_t initval)
{
	uint32_t a, b, c;

	/* Set up the internal state */
	a = b = c = 0xdeadbeef + (((uint32_t) length) << 2) + initval;

	/*----------------------------------------- handle most of the key */
	while (length > 3) {
		a += k[0];
		b += k[1];
		c += k[2];
		mix(a, b, c);
		length -= 3;
		k += 3;
	}

	/*----------------------------------- handle the last 3 uint32_t's */
	switch (length) { /* all the case statements fall through */
	case 3:
		c += k[2]; /* fall through */
	case 2:
		b += k[1]; /* fall through */
	case 1:
		a += k[0];
		final(a, b, c);
	case 0: /* case 0: nothing left to add */
		break;
	}
	/*---------------------------------------------- report the result */
	return c;
}

/*
 * hashword2() -- same as hashword(), but take two seeds and return two 32-bit
 * values.  pc and pb must both be nonnull, and *pc and *pb must both be
 * initialized with seeds.  If you pass in (*pb)==0, the output (*pc) will be
 * the same as the return value from hashword().
 */
static void __attribute__((unused))
hashword2(const uint32_t *k, size_t length, uint32_t *pc, uint32_t *pb)
{
	uint32_t a, b, c;

	/* Set up the internal state */
	a = b = c = 0xdeadbeef + ((uint32_t) (length << 2)) + *pc;
	c += *pb;

	while (length > 3) {
		a += k[0];
		b += k[1];
		c += k[2];
		mix(a, b, c);
		length -= 3;
		k += 3;
	}

	switch (length) {
	case 3:
		c += k[2];
		/* fall through */
	case 2:
		b += k[1];
		/* fall through */
	case 1:
		a += k[0];
		final(a, b, c);
		/* fall through */
	case 0: /* case 0: nothing left to add */
		break;
	}

	*pc = c;
	*pb = b;
}

/*
 * hashlittle() -- hash a variable-length key into a 32-bit value
 *   k       : the key (the unaligned variable-length array of bytes)
 *   length  : the length of the key, counting by bytes
 *   initval : can be any 4-byte value
 * Returns a 32-bit value.  Every bit of the key affects every bit of
 * the return value.  Two keys differing by one or two bits will have
 * totally different hash values.
 *
 * The best hash table sizes are powers of 2.  There is no need to do
 * mod a prime (mod is sooo slow!).  If you need less than 32 bits,
 * use a bitmask.  For example, if you need only 10 bits, do
 *   h = (h & hashmask(10));
 * In which case, the hash table should have hashsize(10) elements.
 *
 * If you are hashing n strings (uint8_t **)k, do it like this:
 *   for (i=0, h=0; i<n; ++i) h = hashlittle( k[i], len[i], h);
 *
 * By Bob Jenkins, 2006.  bob_jenkins@burtleburtle.net.  You may use this
 * code any way you wish, private, educational, or commercial.  It's free.
 *
 * Use for hash table lookup, or anything where one collision in 2^^32 is
 * acceptable.  Do NOT use for cryptographic purposes.
 */
LTTNG_NO_SANITIZE_ADDRESS
__attribute__((unused)) static uint32_t hashlittle(const void *key, size_t length, uint32_t initval)
{
	uint32_t a, b, c;
	union {
		const void *ptr;
		size_t i;
	} u; /* needed for Mac Powerbook G4 */

	/* Set up the internal state */
	a = b = c = 0xdeadbeef + ((uint32_t) length) + initval;

	u.ptr = key;
	if (HASH_LITTLE_ENDIAN && ((u.i & 0x3) == 0)) {
		const uint32_t *k = (const uint32_t *) key; /* read 32-bit chunks */

		/*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
		while (length > 12) {
			a += k[0];
			b += k[1];
			c += k[2];
			mix(a, b, c);
			length -= 12;
			k += 3;
		}

		/*
		 * "k[2]&0xffffff" actually reads beyond the end of the string, but
		 * then masks off the part it's not allowed to read.  Because the
		 * string is aligned, the masked-off tail is in the same word as the
		 * rest of the string.  Every machine with memory protection I've seen
		 * does it on word boundaries, so is OK with this.  But VALGRIND will
		 * still catch it and complain.  The masking trick does make the hash
		 * noticably faster for short strings (like English words).
		 */
#ifndef VALGRIND

		switch (length) {
		case 12:
			c += k[2];
			b += k[1];
			a += k[0];
			break;
		case 11:
			c += k[2] & 0xffffff;
			b += k[1];
			a += k[0];
			break;
		case 10:
			c += k[2] & 0xffff;
			b += k[1];
			a += k[0];
			break;
		case 9:
			c += k[2] & 0xff;
			b += k[1];
			a += k[0];
			break;
		case 8:
			b += k[1];
			a += k[0];
			break;
		case 7:
			b += k[1] & 0xffffff;
			a += k[0];
			break;
		case 6:
			b += k[1] & 0xffff;
			a += k[0];
			break;
		case 5:
			b += k[1] & 0xff;
			a += k[0];
			break;
		case 4:
			a += k[0];
			break;
		case 3:
			a += k[0] & 0xffffff;
			break;
		case 2:
			a += k[0] & 0xffff;
			break;
		case 1:
			a += k[0] & 0xff;
			break;
		case 0:
			return c; /* zero length strings require no mixing */
		}
#else /* make valgrind happy */
		const uint8_t *k8;

		k8 = (const uint8_t *) k;
		switch (length) {
		case 12:
			c += k[2];
			b += k[1];
			a += k[0];
			break;
		case 11:
			c += ((uint32_t) k8[10]) << 16; /* fall through */
		case 10:
			c += ((uint32_t) k8[9]) << 8; /* fall through */
		case 9:
			c += k8[8]; /* fall through */
		case 8:
			b += k[1];
			a += k[0];
			break;
		case 7:
			b += ((uint32_t) k8[6]) << 16; /* fall through */
		case 6:
			b += ((uint32_t) k8[5]) << 8; /* fall through */
		case 5:
			b += k8[4]; /* fall through */
		case 4:
			a += k[0];
			break;
		case 3:
			a += ((uint32_t) k8[2]) << 16; /* fall through */
		case 2:
			a += ((uint32_t) k8[1]) << 8; /* fall through */
		case 1:
			a += k8[0];
			break;
		case 0:
			return c;
		}
#endif /* !valgrind */
	} else if (HASH_LITTLE_ENDIAN && ((u.i & 0x1) == 0)) {
		const uint16_t *k = (const uint16_t *) key; /* read 16-bit chunks */
		const uint8_t *k8;

		/*--------------- all but last block: aligned reads and different mixing */
		while (length > 12) {
			a += k[0] + (((uint32_t) k[1]) << 16);
			b += k[2] + (((uint32_t) k[3]) << 16);
			c += k[4] + (((uint32_t) k[5]) << 16);
			mix(a, b, c);
			length -= 12;
			k += 6;
		}

		k8 = (const uint8_t *) k;
		switch (length) {
		case 12:
			c += k[4] + (((uint32_t) k[5]) << 16);
			b += k[2] + (((uint32_t) k[3]) << 16);
			a += k[0] + (((uint32_t) k[1]) << 16);
			break;
		case 11:
			c += ((uint32_t) k8[10]) << 16; /* fall through */
		case 10:
			c += k[4];
			b += k[2] + (((uint32_t) k[3]) << 16);
			a += k[0] + (((uint32_t) k[1]) << 16);
			break;
		case 9:
			c += k8[8]; /* fall through */
		case 8:
			b += k[2] + (((uint32_t) k[3]) << 16);
			a += k[0] + (((uint32_t) k[1]) << 16);
			break;
		case 7:
			b += ((uint32_t) k8[6]) << 16; /* fall through */
		case 6:
			b += k[2];
			a += k[0] + (((uint32_t) k[1]) << 16);
			break;
		case 5:
			b += k8[4]; /* fall through */
		case 4:
			a += k[0] + (((uint32_t) k[1]) << 16);
			break;
		case 3:
			a += ((uint32_t) k8[2]) << 16; /* fall through */
		case 2:
			a += k[0];
			break;
		case 1:
			a += k8[0];
			break;
		case 0:
			return c; /* zero length requires no mixing */
		}

	} else { /* need to read the key one byte at a time */
		const uint8_t *k = (const uint8_t *) key;

		while (length > 12) {
			a += k[0];
			a += ((uint32_t) k[1]) << 8;
			a += ((uint32_t) k[2]) << 16;
			a += ((uint32_t) k[3]) << 24;
			b += k[4];
			b += ((uint32_t) k[5]) << 8;
			b += ((uint32_t) k[6]) << 16;
			b += ((uint32_t) k[7]) << 24;
			c += k[8];
			c += ((uint32_t) k[9]) << 8;
			c += ((uint32_t) k[10]) << 16;
			c += ((uint32_t) k[11]) << 24;
			mix(a, b, c);
			length -= 12;
			k += 12;
		}

		switch (length) { /* all the case statements fall through */
		case 12:
			c += ((uint32_t) k[11]) << 24; /* fall through */
		case 11:
			c += ((uint32_t) k[10]) << 16; /* fall through */
		case 10:
			c += ((uint32_t) k[9]) << 8; /* fall through */
		case 9:
			c += k[8]; /* fall through */
		case 8:
			b += ((uint32_t) k[7]) << 24; /* fall through */
		case 7:
			b += ((uint32_t) k[6]) << 16; /* fall through */
		case 6:
			b += ((uint32_t) k[5]) << 8; /* fall through */
		case 5:
			b += k[4]; /* fall through */
		case 4:
			a += ((uint32_t) k[3]) << 24; /* fall through */
		case 3:
			a += ((uint32_t) k[2]) << 16; /* fall through */
		case 2:
			a += ((uint32_t) k[1]) << 8; /* fall through */
		case 1:
			a += k[0];
			break;
		case 0:
			return c;
		}
	}

	final(a, b, c);
	return c;
}

unsigned long hash_key_u64(const void *_key, unsigned long seed)
{
	union {
		uint64_t v64;
		uint32_t v32[2];
	} v;
	union {
		uint64_t v64;
		uint32_t v32[2];
	} key;

	v.v64 = (uint64_t) seed;
	key.v64 = *(const uint64_t *) _key;
	hashword2(key.v32, 2, &v.v32[0], &v.v32[1]);
	return v.v64;
}

#if (CAA_BITS_PER_LONG == 64)
/*
 * Hash function for number value.
 * Pass the value itself as the key, not its address.
 */
unsigned long hash_key_ulong(const void *_key, unsigned long seed)
{
	uint64_t __key = (uint64_t) _key;
	return (unsigned long) hash_key_u64(&__key, seed);
}
#else
/*
 * Hash function for number value.
 * Pass the value itself as the key, not its address.
 */
unsigned long hash_key_ulong(const void *_key, unsigned long seed)
{
	uint32_t key = (uint32_t) _key;

	return hashword(&key, 1, seed);
}
#endif /* CAA_BITS_PER_LONG */

/*
 * Hash function for string.
 */
unsigned long hash_key_str(const void *key, unsigned long seed)
{
	return hashlittle(key, strlen((const char *) key), seed);
}

/*
 * Hash function for two uint64_t.
 */
unsigned long hash_key_two_u64(const void *key, unsigned long seed)
{
	const struct lttng_ht_two_u64 *k = (const struct lttng_ht_two_u64 *) key;

	return hash_key_u64(&k->key1, seed) ^ hash_key_u64(&k->key2, seed);
}

/*
 * Hash function compare for number value.
 */
int hash_match_key_ulong(const void *key1, const void *key2)
{
	if (key1 == key2) {
		return 1;
	}

	return 0;
}

/*
 * Hash function compare for number value.
 */
int hash_match_key_u64(const void *key1, const void *key2)
{
	if (*(const uint64_t *) key1 == *(const uint64_t *) key2) {
		return 1;
	}

	return 0;
}

/*
 * Hash compare function for string.
 */
int hash_match_key_str(const void *key1, const void *key2)
{
	if (strcmp((const char *) key1, (const char *) key2) == 0) {
		return 1;
	}

	return 0;
}

/*
 * Hash function compare two uint64_t.
 */
int hash_match_key_two_u64(const void *key1, const void *key2)
{
	const struct lttng_ht_two_u64 *k1 = (const struct lttng_ht_two_u64 *) key1;
	const struct lttng_ht_two_u64 *k2 = (const struct lttng_ht_two_u64 *) key2;

	if (hash_match_key_u64(&k1->key1, &k2->key1) && hash_match_key_u64(&k1->key2, &k2->key2)) {
		return 1;
	}

	return 0;
}
