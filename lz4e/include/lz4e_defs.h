#ifndef __LZ4DEFS_H__
#define __LZ4DEFS_H__

/*
 * lz4defs.h -- common and architecture specific defines for the kernel usage

 * LZ4 - Fast LZ compression algorithm
 * Copyright (C) 2011-2016, Yann Collet.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * You can contact the author at :
 *	- LZ4 homepage : http://www.lz4.org
 *	- LZ4 source repository : https://github.com/lz4/lz4
 *
 *	Changed for kernel usage by:
 *	Sven Schmidt <4sschmid@informatik.uni-hamburg.de>
 */

// TODO:(kogora)[f]: mind about LICENSE |^


#include <linux/bio.h>
#include <linux/bitops.h>
#include <linux/bvec.h>
#include <linux/highmem.h>
#include <linux/minmax.h>
#include <linux/string.h>	 /* memset, memcpy */
#include <linux/unaligned.h>

#include "lz4e.h"

#define FORCE_INLINE __always_inline

/*-************************************
 *	Basic Types
 **************************************/
#include <linux/types.h>

typedef	uint8_t BYTE;
typedef uint16_t U16;
typedef uint32_t U32;
typedef	int32_t S32;
typedef uint64_t U64;
typedef uintptr_t uptrval;

/*-************************************
 *	Architecture specifics
 **************************************/
#if defined(CONFIG_64BIT)
#define LZ4_ARCH64 1
#else
#define LZ4_ARCH64 0
#endif

#if defined(__LITTLE_ENDIAN)
#define LZ4_LITTLE_ENDIAN 1
#else
#define LZ4_LITTLE_ENDIAN 0
#endif

/*-************************************
 *	Constants
 **************************************/
#define MINMATCH 4

#define WILDCOPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT (WILDCOPYLENGTH + MINMATCH)
#define LZ4E_MIN_LENGTH (MFLIMIT + 1)
/*
 * ensure it's possible to write 2 x wildcopyLength
 * without overflowing output buffer
 */
#define MATCH_SAFEGUARD_DISTANCE  ((2 * WILDCOPYLENGTH) - MINMATCH)

/* Increase this value ==> compression run slower on incompressible data */
#define LZ4_SKIPTRIGGER 6

#define HASH_UNIT sizeof(size_t)

#define KB (1 << 10)
#define MB (1 << 20)
#define GB (1U << 30)

#define MAX_DISTANCE LZ4E_DISTANCE_MAX
#define STEPSIZE sizeof(size_t)

#define ML_BITS	4
#define ML_MASK	((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

/*-************************************
 *	Bvec iterator helpers
 **************************************/
#define LZ4E_ITER_POS(iter, start) \
	(((start).bi_size) - ((iter).bi_size))

#define LZ4E_for_each_bvec(bvl, bio_vec, iter, start) \
	for (iter = (start); \
	     ((iter).bi_size) && \
		((bvl = (bio_vec)[((iter).bi_idx)]), 1); \
	     bvec_iter_advance_single((bio_vec), &(iter), \
		mp_bvec_iter_len((bio_vec), (iter))))

/*
 * advance bvec iterator by exactly 1 byte
 */
static FORCE_INLINE void LZ4E_iter_advance1(const struct bio_vec *bvecs,
		struct bvec_iter *iter)
{
	unsigned idx = iter->bi_idx;
	unsigned done = iter->bi_bvec_done;

	BUG_ON(iter->bi_size == 0);

	done++;

	if (done == bvecs[idx].bv_len) {
		idx++;
		done = 0;
	}

	iter->bi_idx = idx;
	iter->bi_bvec_done = done;
	iter->bi_size--;
}

/*
 * roll bvec iterator back by exactly 1 byte
 */
static FORCE_INLINE void LZ4E_iter_rollback1(const struct bio_vec *bvecs,
		struct bvec_iter *iter)
{
	unsigned idx = iter->bi_idx;
	unsigned done = iter->bi_bvec_done;

	if (done == 0) {
		BUG_ON(idx == 0);

		idx--;
		done = bvecs[idx].bv_len;
	}

	done--;

	iter->bi_idx = idx;
	iter->bi_bvec_done = done;
	iter->bi_size++;
}

static FORCE_INLINE void LZ4E_advance(
	const struct bio_vec *bvecs,
	struct bvec_iter *iter,
	U32 *pos,
	const unsigned bytes)
{
	bvec_iter_advance(bvecs, iter, bytes);
	*pos += bytes;
}

static FORCE_INLINE void LZ4E_advance1(
	const struct bio_vec *bvecs,
	struct bvec_iter *iter,
	U32 *pos)
{
	LZ4E_iter_advance1(bvecs, iter);
	(*pos)++;
}

static FORCE_INLINE void LZ4E_rollback1(
	const struct bio_vec *bvecs,
	struct bvec_iter *iter,
	U32 *pos)
{
	LZ4E_iter_rollback1(bvecs, iter);
	(*pos)--;
}

/*-************************************
 *	Reading and writing into memory
 **************************************/
static FORCE_INLINE U16 LZ4_read16(const void *ptr)
{
	return get_unaligned((const U16 *)ptr);
}

static FORCE_INLINE U32 LZ4_read32(const void *ptr)
{
	return get_unaligned((const U32 *)ptr);
}

static FORCE_INLINE size_t LZ4_read_ARCH(const void *ptr)
{
	return get_unaligned((const size_t *)ptr);
}

static FORCE_INLINE void LZ4_write16(void *memPtr, U16 value)
{
	put_unaligned(value, (U16 *)memPtr);
}

static FORCE_INLINE void LZ4_write32(void *memPtr, U32 value)
{
	put_unaligned(value, (U32 *)memPtr);
}

static FORCE_INLINE U16 LZ4_readLE16(const void *memPtr)
{
	return get_unaligned_le16(memPtr);
}

static FORCE_INLINE void LZ4_writeLE16(void *memPtr, U16 value)
{
	return put_unaligned_le16(value, memPtr);
}

/*
 * LZ4 relies on memcpy with a constant size being inlined. In freestanding
 * environments, the compiler can't assume the implementation of memcpy() is
 * standard compliant, so apply its specialized memcpy() inlining logic. When
 * possible, use __builtin_memcpy() to tell the compiler to analyze memcpy()
 * as-if it were standard compliant, so it can inline it in freestanding
 * environments. This is needed when decompressing the Linux Kernel, for example.
 */
#define LZ4_memcpy(dst, src, size) __builtin_memcpy(dst, src, size)
#define LZ4_memmove(dst, src, size) __builtin_memmove(dst, src, size)

static FORCE_INLINE void LZ4_copy8(void *dst, const void *src)
{
#if LZ4_ARCH64
	U64 a = get_unaligned((const U64 *)src);

	put_unaligned(a, (U64 *)dst);
#else
	U32 a = get_unaligned((const U32 *)src);
	U32 b = get_unaligned((const U32 *)src + 1);

	put_unaligned(a, (U32 *)dst);
	put_unaligned(b, (U32 *)dst + 1);
#endif
}

/*
 * customized variant of memcpy,
 * which can overwrite up to 7 bytes beyond dstEnd
 */
static FORCE_INLINE void LZ4_wildCopy(void *dstPtr,
	const void *srcPtr, void *dstEnd)
{
	BYTE *d = (BYTE *)dstPtr;
	const BYTE *s = (const BYTE *)srcPtr;
	BYTE *const e = (BYTE *)dstEnd;

	do {
		LZ4_copy8(d, s);
		d += 8;
		s += 8;
	} while (d < e);
}

static FORCE_INLINE unsigned int LZ4_NbCommonBytes(register size_t val)
{
#if LZ4_LITTLE_ENDIAN
	return (unsigned)(__ffs(val) >> 3);
#else
	return (BITS_PER_LONG - 1 - __fls(val)) >> 3;
#endif
}

static FORCE_INLINE unsigned int LZ4_count(
	const BYTE *pIn,
	const BYTE *pMatch,
	const BYTE *pInLimit)
{
	const BYTE *const pStart = pIn;

	while (likely(pIn < pInLimit - (STEPSIZE - 1))) {
		size_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);

		if (!diff) {
			pIn += STEPSIZE;
			pMatch += STEPSIZE;
			continue;
		}

		pIn += LZ4_NbCommonBytes(diff);

		return (unsigned int)(pIn - pStart);
	}

#if LZ4_ARCH64
	if ((pIn < (pInLimit - 3))
		&& (LZ4_read32(pMatch) == LZ4_read32(pIn))) {
		pIn += 4;
		pMatch += 4;
	}
#endif

	if ((pIn < (pInLimit - 1))
		&& (LZ4_read16(pMatch) == LZ4_read16(pIn))) {
		pIn += 2;
		pMatch += 2;
	}

	if ((pIn < pInLimit) && (*pMatch == *pIn))
		pIn++;

	return (unsigned int)(pIn - pStart);
}

/*-************************************
 *	Extended memory management
 **************************************/
static FORCE_INLINE void LZ4E_swap(char * const first, char * const second)
{
	char tmp = *first;
	*first = *second;
	*second = tmp;
}

static FORCE_INLINE U16 LZ4E_toLE16(U16 value)
{
#if LZ4_LITTLE_ENDIAN
	return value;
#else
	char *ptr = (char *)(&value);
	LZ4E_swap(ptr, ptr + 1);
	return value;
#endif
}

static FORCE_INLINE void LZ4E_memcpy_from_bvec(char *to,
		const struct bio_vec *from, const size_t len)
{
	BUG_ON(len > from->bv_len || from->bv_offset + len > PAGE_SIZE);
	memcpy_from_page(to, from->bv_page, from->bv_offset, len);
}

static FORCE_INLINE void LZ4E_memcpy_to_bvec(struct bio_vec *to,
		const char *from, const size_t len)
{
	BUG_ON(len > to->bv_len || to->bv_offset + len > PAGE_SIZE);
	memcpy_to_page(to->bv_page, to->bv_offset, from, len);
}

static FORCE_INLINE void LZ4E_memcpy_from_sg(char *to, const struct bio_vec *from,
		struct bvec_iter iter, size_t len)
{
	struct bio_vec curBvec;
	size_t toRead;

	BUG_ON(len > iter.bi_size);

	while (len) {
		curBvec = bvec_iter_bvec(from, iter);
		toRead = min_t(size_t, len, curBvec.bv_len);

		LZ4E_memcpy_from_bvec(to, &curBvec, toRead);
		bvec_iter_advance_single(from, &iter, (unsigned)toRead);
		to += toRead;
		len -= toRead;
	}
}

static FORCE_INLINE void LZ4E_memcpy_to_sg(struct bio_vec *to, const char *from,
		struct bvec_iter iter, size_t len)
{
	struct bio_vec curBvec;
	size_t toWrite;

	BUG_ON(len > iter.bi_size);

	while (len) {
		curBvec = bvec_iter_bvec(to, iter);
		toWrite = min_t(size_t, len, curBvec.bv_len);

		LZ4E_memcpy_to_bvec(&curBvec, from, toWrite);
		bvec_iter_advance_single(to, &iter, (unsigned)toWrite);
		from += toWrite;
		len -= toWrite;
	}
}

static FORCE_INLINE BYTE LZ4E_read8(const struct bio_vec *from,
		struct bvec_iter iter)
{
	BYTE ret;

	LZ4E_memcpy_from_sg(&ret, from, iter, 1);
	return ret;
}

static FORCE_INLINE void LZ4E_write8(struct bio_vec *to, const BYTE value,
		struct bvec_iter iter)
{
	LZ4E_memcpy_to_sg(to, &value, iter, 1);
}

static FORCE_INLINE U16 LZ4E_read16(const struct bio_vec *from,
		struct bvec_iter iter)
{
	U16 ret;

	LZ4E_memcpy_from_sg((char *)(&ret), from, iter, 2);
	return ret;
}

static FORCE_INLINE void LZ4E_write16(struct bio_vec *to, const U16 value,
		struct bvec_iter iter)
{
	LZ4E_memcpy_to_sg(to, (char *)(&value), iter, 2);
}

static FORCE_INLINE U16 LZ4E_readLE16(const struct bio_vec *from,
		struct bvec_iter iter)
{
	U16 ret;

	LZ4E_memcpy_from_sg((char *)(&ret), from, iter, 2);
	return LZ4E_toLE16(ret);
}

static FORCE_INLINE void LZ4E_writeLE16(struct bio_vec *to, const U16 value,
		struct bvec_iter iter)
{
	U16 valueLE = LZ4E_toLE16(value);

	LZ4E_memcpy_to_sg(to, (char *)(&valueLE), iter, 2);
}

static FORCE_INLINE U32 LZ4E_read32(const struct bio_vec *from,
		struct bvec_iter iter)
{
	U32 ret;

	LZ4E_memcpy_from_sg((char *)(&ret), from, iter, 4);
	return ret;
}

static FORCE_INLINE void LZ4E_write32(struct bio_vec *to, const U32 value,
		struct bvec_iter iter)
{
	LZ4E_memcpy_to_sg(to, (char *)(&value), iter, 4);
}

static FORCE_INLINE U64 LZ4E_read64(const struct bio_vec *from,
		struct bvec_iter iter)
{
	U64 ret;

	LZ4E_memcpy_from_sg((char *)(&ret), from, iter, 8);
	return ret;
}

static FORCE_INLINE void LZ4E_write64(struct bio_vec *to, const U64 value,
		struct bvec_iter iter)
{
	LZ4E_memcpy_to_sg(to, (char *)(&value), iter, 8);
}

static FORCE_INLINE size_t LZ4E_readArch(const struct bio_vec *from,
		struct bvec_iter iter)
{
#if LZ4_ARCH64
	return (size_t)LZ4E_read64(from, iter);
#else
	return (size_t)LZ4E_read32(from, iter);
#endif
}

static FORCE_INLINE void LZ4E_writeArch(struct bio_vec *to, const size_t value,
		struct bvec_iter iter)
{
#if LZ4_ARCH64
	LZ4E_write64(to, (U64)value, iter);
#else
	LZ4E_write32(to, (U32)value, iter);
#endif
}

static FORCE_INLINE void LZ4E_copy8(struct bio_vec *dst, const struct bio_vec *src,
		struct bvec_iter dstIter, struct bvec_iter srcIter)
{
	BYTE val = LZ4E_read8(src, srcIter);

	LZ4E_write8(dst, val, dstIter);
}

static FORCE_INLINE void LZ4E_copy16(struct bio_vec *dst, const struct bio_vec *src,
		struct bvec_iter dstIter, struct bvec_iter srcIter)
{
	U16 val = LZ4E_read16(src, srcIter);

	LZ4E_write16(dst, val, dstIter);
}

static FORCE_INLINE void LZ4E_copy32(struct bio_vec *dst, const struct bio_vec *src,
		struct bvec_iter dstIter, struct bvec_iter srcIter)
{
	U32 val = LZ4E_read32(src, srcIter);

	LZ4E_write32(dst, val, dstIter);
}

static FORCE_INLINE void LZ4E_copy64(struct bio_vec *dst, const struct bio_vec *src,
		struct bvec_iter dstIter, struct bvec_iter srcIter)
{
#if LZ4_ARCH64
	U64 val = LZ4E_read64(src, srcIter);

	LZ4E_write64(dst, val, dstIter);
#else
	U32 a = LZ4E_read32(src, srcIter);
	bvec_iter_advance(src, &srcIter, 4)
	U32 b = LZ4E_read32(src, srcIter);

	LZ4E_write32(dst, a, dstIter);
	bvec_iter_advance(dst, &dstIter, 4);
	LZ4E_write32(dst, b, dstIter);
#endif
}

/*
 * customized variant of memcpy,
 * which can overwrite up to 7 bytes beyond target len
 */
static FORCE_INLINE void LZ4E_wildCopy(struct bio_vec *dst, const struct bio_vec *src,
	struct bvec_iter dstIter, struct bvec_iter srcIter, size_t len)
{
	while (len) {
		LZ4E_copy64(dst, src, dstIter, srcIter);
		bvec_iter_advance(src, &srcIter, WILDCOPYLENGTH);
		bvec_iter_advance(dst, &dstIter, WILDCOPYLENGTH);
		len -= min_t(size_t, len, WILDCOPYLENGTH);
	}
}

static FORCE_INLINE void LZ4E_memcpy(struct bio_vec *dst, const struct bio_vec *src,
	struct bvec_iter dstIter, struct bvec_iter srcIter, size_t len)
{
	for (int i = 0; i < len / 8; ++i) {
		LZ4E_copy64(dst, src, dstIter, srcIter);
		bvec_iter_advance(src, &srcIter, 8);
		bvec_iter_advance(dst, &dstIter, 8);
	}

	len %= 8;

	if (len >= 4) {
		LZ4E_copy32(dst, src, dstIter, srcIter);
		bvec_iter_advance(src, &srcIter, 4);
		bvec_iter_advance(dst, &dstIter, 4);
		len -= 4;
	}

	if (len >= 2) {
		LZ4E_copy16(dst, src, dstIter, srcIter);
		bvec_iter_advance(src, &srcIter, 2);
		bvec_iter_advance(dst, &dstIter, 2);
		len -= 2;
	}

	if (len)
		LZ4E_copy8(dst, src, dstIter, srcIter);
}

static FORCE_INLINE unsigned LZ4E_count(
	const struct bio_vec *bvecs,
	struct bvec_iter inIter,
	struct bvec_iter matchIter,
	const unsigned countLimit)
{
	unsigned count = 0;

	for (int i = 0; i < countLimit / STEPSIZE; ++i) {
		size_t const inVal = LZ4E_readArch(bvecs, inIter);
		size_t const matchVal = LZ4E_readArch(bvecs, matchIter);
		size_t const diff = inVal ^ matchVal;

		if (diff) {
			count += LZ4_NbCommonBytes(diff);
			return count;
		}

		count += STEPSIZE;

		bvec_iter_advance(bvecs, &inIter, STEPSIZE);
		bvec_iter_advance(bvecs, &matchIter, STEPSIZE);
	}

	unsigned rem = countLimit % STEPSIZE;

#if LZ4_ARCH64
	if (rem >= 4 && LZ4E_read32(bvecs, inIter)
			== LZ4E_read32(bvecs, matchIter)) {
		count += 4;
		rem -= 4;
		bvec_iter_advance(bvecs, &inIter, 4);
		bvec_iter_advance(bvecs, &matchIter, 4);
	}
#endif

	if (rem >= 2 && LZ4E_read16(bvecs, inIter)
			== LZ4E_read16(bvecs, matchIter)) {
		count += 2;
		rem -= 2;
		bvec_iter_advance(bvecs, &inIter, 2);
		bvec_iter_advance(bvecs, &matchIter, 2);
	}

	if (rem && LZ4E_read8(bvecs, inIter)
			== LZ4E_read8(bvecs, matchIter))
		count++;

	return count;
}

/*-************************************
 *	Hash table addresses
 **************************************/
typedef union {
	struct {
		/* Max 16 bvecs by 4 kilobytes */
		BYTE bvec_idx : 4;
		U16 bvec_off  : 12;
	} addr;
	U16 raw;
} __packed LZ4E_tbl_addr16_t;

typedef union {
	struct {
		/* Max 256 bvecs by 16 megabytes */
		BYTE bvec_idx : 8;
		U32 bvec_off  : 24;
	} addr;
	U32 raw;
} __packed LZ4E_tbl_addr32_t;

typedef union {
	struct {
		/* Max 256 bvecs of maximum size */
		BYTE bvec_idx : 8;
		U32 bvec_off  : 32;
		/* 24 bytes reserved */
	} addr;
	U64 raw;
} __packed LZ4E_tbl_addr64_t;

#define LZ4E_TBL_ADDR16_IDX_LIMIT (1 << 4)
#define LZ4E_TBL_ADDR16_OFF_LIMIT (1 << 12)
#define LZ4E_TBL_ADDR32_OFF_LIMIT (1 << 24)

#define LZ4E_TBL_ADDR_FROM_ITER(addrType, iter, baseIter) \
	((addrType) { \
		.addr = { \
			.bvec_idx = (((iter).bi_idx) - ((baseIter).bi_idx)), \
			.bvec_off = ((iter).bi_bvec_done) \
		} \
	})

#define LZ4E_TBL_ADDR_TO_ITER(addr, baseIter, bvIterSize) \
	((struct bvec_iter) { \
		.bi_idx = (((addr).addr.bvec_idx) + ((baseIter).bi_idx)), \
		.bi_size = (((bvIterSize)[(addr).addr.bvec_idx]) - ((addr).addr.bvec_off)), \
		.bi_bvec_done = ((addr).addr.bvec_off) \
	 })

typedef enum { noLimit = 0, limitedOutput = 1 } limitedOutput_directive;
typedef enum { byU16 = 1, byU32 = 3, byU64 = 7 } tableType_t;

typedef enum { noDict = 0, withPrefix64k, usingExtDict } dict_directive;
typedef enum { noDictIssue = 0, dictSmall } dictIssue_directive;

typedef enum { endOnOutputSize = 0, endOnInputSize = 1 } endCondition_directive;
typedef enum { decode_full_block = 0, partial_decode = 1 } earlyEnd_directive;

#define LZ4_STATIC_ASSERT(c)	BUILD_BUG_ON(!(c))

#endif
