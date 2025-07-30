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
#include <linux/lz4.h>

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

#define MAX_DISTANCE LZ4_DISTANCE_MAX
#define STEPSIZE 8

#define ML_BITS	4
#define ML_MASK	((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

/*-************************************
 *	Bvec iterator helpers
 **************************************/
#define LZ4E_ITER_POS(iter, start) \
	((start).bi_size - (iter).bi_size)

/*
 * advance bvec iterator by exactly 1 byte
 */
static FORCE_INLINE void LZ4E_iter_advance1(const struct bio_vec *sgBuf,
		struct bvec_iter *iter)
{
	unsigned idx = iter->bi_idx;
	unsigned done = iter->bi_bvec_done;
	struct bio_vec bvec = bvec_iter_bvec(sgBuf, *iter);

	BUG_ON(iter->bi_size == 0);

	done++;

	if (done == bvec.bv_len) {
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
static FORCE_INLINE void LZ4E_iter_rollback1(const struct bio_vec *sgBuf,
		struct bvec_iter *iter)
{
	unsigned idx = iter->bi_idx;
	unsigned done = iter->bi_bvec_done;

	if (done == 0) {
		BUG_ON(idx == 0);

		idx--;
		done = sgBuf[idx].bv_len;
	}

	done--;

	iter->bi_idx = idx;
	iter->bi_bvec_done = done;
	iter->bi_size++;
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
#endif

	char *ptr = (char *)(&value);
	LZ4E_swap(ptr, ptr + 1);
	return value;
}

static FORCE_INLINE void LZ4E_memcpy_from_bvec(char *to, const struct bio_vec *from,
		const size_t off, const size_t len)
{
	memcpy_from_page(to, from->bv_page, from->bv_offset + off, len);
}

static FORCE_INLINE void LZ4E_memcpy_to_bvec(struct bio_vec *to, const char *from,
		const size_t off, const size_t len)
{
	memcpy_to_page(to->bv_page, to->bv_offset + off, from, len);
}

static FORCE_INLINE void LZ4E_memcpy_from_sg(char *to, const struct bio_vec *from,
		struct bvec_iter iter, size_t len)
{
	struct bio_vec curBvec;
	unsigned off;
	size_t toRead;

	BUG_ON(len > iter.bi_size);

	while (len) {
		curBvec = bvec_iter_bvec(from, iter);
		off = iter.bi_bvec_done;
		toRead = min_t(size_t, len, curBvec.bv_len - off);

		LZ4E_memcpy_from_bvec(to, &curBvec, off, toRead);
		bvec_iter_advance_single(from, &iter, (unsigned)toRead);
		to += toRead;
		len -= toRead;
	}
}

static FORCE_INLINE void LZ4E_memcpy_to_sg(struct bio_vec *to, const char *from,
		struct bvec_iter iter, size_t len)
{
	struct bio_vec curBvec;
	unsigned off;
	size_t toWrite;

	BUG_ON(len > iter.bi_size);

	while (len) {
		curBvec = bvec_iter_bvec(to, iter);
		off = iter.bi_bvec_done;
		toWrite = min_t(size_t, len, curBvec.bv_len - off);

		LZ4E_memcpy_to_bvec(&curBvec, from, off, toWrite);
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
	U64 a = LZ4E_read64(src, srcIter);

	LZ4E_write64(dst, a, dstIter);
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
	do {
		LZ4E_copy64(dst, src, dstIter, srcIter);
		bvec_iter_advance(src, &srcIter, WILDCOPYLENGTH);
		bvec_iter_advance(dst, &dstIter, WILDCOPYLENGTH);
		len -= WILDCOPYLENGTH;
	} while (len > 0);
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

static FORCE_INLINE unsigned int LZ4E_NbCommonBytes(register size_t val)
{
#if LZ4_LITTLE_ENDIAN
	return (unsigned)(__ffs(val) >> 3);
#else
	return (BITS_PER_LONG - 1 - __fls(val)) >> 3;
#endif
}

static FORCE_INLINE unsigned int LZ4E_count(
	const struct bio_vec *sgBuf,
	struct bvec_iter inIter,
	struct bvec_iter matchIter,
	const size_t inLimit)
{
	const struct bvec_iter inStart = inIter;
	U32 inPos = 0;

	while (likely(inPos < inLimit - (STEPSIZE - 1))) {
		U64 const inVal = LZ4E_read64(sgBuf, inIter);
		U64 const matchVal = LZ4E_read64(sgBuf, matchIter);
		U64 const diff = inVal ^ matchVal;

		if (!diff) {
			bvec_iter_advance(sgBuf, &inIter, STEPSIZE);
			bvec_iter_advance(sgBuf, &matchIter, STEPSIZE);
			inPos = LZ4E_ITER_POS(inIter, inStart);
			continue;
		}

		inPos += LZ4E_NbCommonBytes(diff);

		return (unsigned)inPos;
	}

#if LZ4_ARCH64
	if ((inPos < (inLimit - (4 - 1)))
		&& (LZ4E_read32(sgBuf, inIter)
			== LZ4E_read32(sgBuf, matchIter))) {
		bvec_iter_advance(sgBuf, &inIter, 4);
		bvec_iter_advance(sgBuf, &matchIter, 4);
		inPos = LZ4E_ITER_POS(inIter, inStart);
	}
#endif

	if ((inPos < (inLimit - (2 - 1)))
		&& (LZ4E_read16(sgBuf, inIter)
			== LZ4E_read16(sgBuf, matchIter))) {
		bvec_iter_advance(sgBuf, &inIter, 2);
		bvec_iter_advance(sgBuf, &matchIter, 2);
		inPos = LZ4E_ITER_POS(inIter, inStart);
	}

	if ((inPos < inLimit)
		&& (LZ4E_read8(sgBuf, inIter)
			== LZ4E_read8(sgBuf, matchIter)))
		inPos++;

	return (unsigned)inPos;
}

/*-************************************
 *	Hash table addresses
 **************************************/
typedef union {
    struct {
        u8 bvec_idx;
        u16 bvec_off;
        /* 8b reserved */
    } addr;
    u32 raw;
} __packed LZ4E_tbl_addr_t;

#define LZ4E_TBL_ADDR_FROM_ITER(sgBuf, iter) \
	((LZ4E_tbl_addr_t) { \
		.raw = ((iter).bi_idx << 24) + ((iter).bi_bvec_done << 8) \
	})

#define LZ4E_TBL_ADDR_TO_ITER(addr, bvRemSize) \
	((struct bvec_iter) { \
		.bi_idx = (addr).addr.bvec_idx, \
		.bi_size = (bvRemSize)[(addr).addr.bvec_idx] - (addr).addr.bvec_off, \
		.bi_bvec_done = (addr).addr.bvec_off \
	 })

typedef enum { noLimit = 0, limitedOutput = 1 } limitedOutput_directive;
typedef enum { byPtr, byU32, byU16 } tableType_t;

typedef enum { noDict = 0, withPrefix64k, usingExtDict } dict_directive;
typedef enum { noDictIssue = 0, dictSmall } dictIssue_directive;

typedef enum { endOnOutputSize = 0, endOnInputSize = 1 } endCondition_directive;
typedef enum { decode_full_block = 0, partial_decode = 1 } earlyEnd_directive;

#define LZ4_STATIC_ASSERT(c)	BUILD_BUG_ON(!(c))

#endif
