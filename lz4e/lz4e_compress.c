/*
 * LZ4 - Fast LZ compression algorithm
 * Copyright (C) 2011 - 2016, Yann Collet.
 * BSD 2 - Clause License (http://www.opensource.org/licenses/bsd - license.php)
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

/*-************************************
 *	Dependencies
 **************************************/
#include <linux/bio.h>
#include <linux/bvec.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "include/lz4e/lz4e.h"
#include "include/lz4e/lz4e_defs.h"

static const int LZ4_minLength = (MFLIMIT + 1);
static const int LZ4_64Klimit = ((64 * KB) + (MFLIMIT - 1));

/*-******************************
 *	Compression functions
 ********************************/
static FORCE_INLINE U32 LZ4_hash4(
	const U32 sequence,
	const tableType_t tableType)
{
	if (tableType == byU16)
		return ((sequence * 2654435761U)
			>> ((MINMATCH * 8) - (LZ4_HASHLOG + 1)));
	else
		return ((sequence * 2654435761U)
			>> ((MINMATCH * 8) - LZ4_HASHLOG));
}

static FORCE_INLINE U32 LZ4_hash5(
	const U64 sequence,
	const tableType_t tableType)
{
	const U32 hashLog = (tableType == byU16)
		? LZ4_HASHLOG + 1
		: LZ4_HASHLOG;

#if LZ4_LITTLE_ENDIAN
	static const U64 prime5bytes = 889523592379ULL;

	return (U32)(((sequence << 24) * prime5bytes) >> (64 - hashLog));
#else
	static const U64 prime8bytes = 11400714785074694791ULL;

	return (U32)(((sequence >> 24) * prime8bytes) >> (64 - hashLog));
#endif
}

static FORCE_INLINE U32 LZ4E_hashPosition(
	const struct bio_vec *bvecs,
	const struct bvec_iter pos,
	const tableType_t tableType)
{
#if LZ4_ARCH64
	if (tableType == byU32)
		return LZ4_hash5(LZ4E_read64(bvecs, pos), tableType);
#endif

	return LZ4_hash4(LZ4E_read32(bvecs, pos), tableType);
}

static void LZ4E_putPositionOnHash(
	const struct bvec_iter pos,
	const U32 h,
	void *tableBase,
	const tableType_t tableType)
{
	LZ4E_tbl_addr_t *hashTable = (LZ4E_tbl_addr_t *)tableBase;

	hashTable[h] = LZ4E_TBL_ADDR_FROM_ITER(pos);
}

static FORCE_INLINE void LZ4E_putPosition(
	const struct bio_vec *bvecs,
	const struct bvec_iter pos,
	void *tableBase,
	const tableType_t tableType)
{
	U32 const h = LZ4E_hashPosition(bvecs, pos, tableType);

	LZ4E_putPositionOnHash(pos, h, tableBase, tableType);
}

static struct bvec_iter LZ4E_getPositionOnHash(
	const U32 h,
	void *tableBase,
	void *biSizeBase,
	const tableType_t tableType,
	const struct bvec_iter baseIter)
{
	const LZ4E_tbl_addr_t *hashTable = (const LZ4E_tbl_addr_t *)tableBase;
	const U32 *bvIterSize = (const U32 *)biSizeBase;
	const LZ4E_tbl_addr_t addr = hashTable[h];

	if (addr.raw == 0)
		return baseIter;

	return LZ4E_TBL_ADDR_TO_ITER(addr, bvIterSize);
}

static FORCE_INLINE struct bvec_iter LZ4E_getPosition(
	const struct bio_vec *bvecs,
	const struct bvec_iter pos,
	void *tableBase,
	void *biSizeBase,
	const tableType_t tableType,
	const struct bvec_iter baseIter)
{
	U32 const h = LZ4E_hashPosition(bvecs, pos, tableType);

	return LZ4E_getPositionOnHash(
			h, tableBase, biSizeBase, tableType, baseIter);
}

static bool LZ4E_fillBvIterSize(
	U32 *bvIterSize,
	const struct bio_vec *bvecs,
	const struct bvec_iter start)
{
	struct bvec_iter iter;
	struct bio_vec curBvec;

	for_each_bvec(curBvec, bvecs, iter, start) {
		if (iter.bi_idx >= BIO_MAX_VECS)
			return false;

		bvIterSize[iter.bi_idx] = iter.bi_size;
	}

	return true;
}


/*
 * LZ4_compress_generic() :
 * inlined, to ensure branches are decided at compilation time
 */
static FORCE_INLINE int LZ4E_compress_generic(
	LZ4E_stream_t_internal * const dictPtr,
	const struct bio_vec * const src,
	struct bio_vec * const dst,
	struct bvec_iter * const srcIter,
	struct bvec_iter * const dstIter,
	const limitedOutput_directive outputLimited,
	const tableType_t tableType,
	const dict_directive dict,			// NOTE:(kogora): always noDict
	const dictIssue_directive dictIssue,		// NOTE:(kogora): always noDictIssue
	const U32 acceleration)
{
	const unsigned int inputSize = srcIter->bi_size;
	const unsigned int maxOutputSize = dstIter->bi_size;
	const struct bvec_iter srcStart = *srcIter;
	struct bvec_iter anchorIter = srcStart;

	const U32 mflimit = inputSize - MFLIMIT;
	const U32 matchlimit = inputSize - LASTLITERALS;

	U32 srcPos = 0;
	U32 dstPos = 0;
	U32 anchorPos = 0;
	U32 forwardH;

	/* Init conditions */
	if (inputSize > LZ4_MAX_INPUT_SIZE) {
		/* Unsupported inputSize, too large (or negative) */
		return 0;
	}

//	switch (dict) {
//	case noDict:
//	default:
//		base = (const BYTE *)source;
//		lowLimit = (const BYTE *)source;
//		break;
//	case withPrefix64k:
//		base = (const BYTE *)source - dictPtr->currentOffset;
//		lowLimit = (const BYTE *)source - dictPtr->dictSize;
//		break;
//	case usingExtDict:
//		base = (const BYTE *)source - dictPtr->currentOffset;
//		lowLimit = (const BYTE *)source;
//		break;
//	}

	if ((tableType == byU16)
		&& (inputSize >= LZ4_64Klimit)) {
		/* Size too large (not within 64K limit) */
		return 0;
	}

	if (inputSize < LZ4_minLength) {
		/* Input too small, no compression (all literals) */
		goto _last_literals;
	}

	/* Fill number of bytes remaining for each bvec */
	if (!LZ4E_fillBvIterSize(dictPtr->bvIterSize, src, srcStart)) {
		/* Too many bvecs */
		return 0;
	}

	/* First Byte */
	LZ4E_putPosition(src, *srcIter, dictPtr->hashTable, tableType);
	LZ4E_advance1(src, srcIter, &srcPos);
	forwardH = LZ4E_hashPosition(src, *srcIter, tableType);

	/* Main Loop */
	for ( ; ; ) {
		BYTE token;
		struct bvec_iter tokenIter;
		struct bvec_iter matchIter;
		U32 matchPos;

		/* Find a match */
		{
			struct bvec_iter forwardIter = *srcIter;
			U32 forwardPos = srcPos;
			unsigned int step = 1;
			unsigned int searchMatchNb = acceleration << LZ4_SKIPTRIGGER;

			do {
				U32 const h = forwardH;

				*srcIter = forwardIter;
				srcPos = forwardPos;
				LZ4E_advance(src, &forwardIter, &forwardPos, step);
				step = (searchMatchNb++ >> LZ4_SKIPTRIGGER);

				if (unlikely(forwardPos > mflimit))
					goto _last_literals;

				matchIter = LZ4E_getPositionOnHash(h,
					dictPtr->hashTable,
					dictPtr->bvIterSize,
					tableType, srcStart);
				matchPos = LZ4E_ITER_POS(matchIter, srcStart);

//				if (dict == usingExtDict) {
//					if (match < (const BYTE *)source) {
//						refDelta = dictDelta;
//						lowLimit = dictionary;
//					} else {
//						refDelta = 0;
//						lowLimit = (const BYTE *)source;
//				}	 }

				forwardH = LZ4E_hashPosition(src,
					forwardIter, tableType);

				LZ4E_putPositionOnHash(*srcIter, h,
					dictPtr->hashTable, tableType);
			} while (((tableType == byU16)
					? 0
					: (matchPos + MAX_DISTANCE < srcPos))
				|| (LZ4E_read32(src, matchIter)
					!= LZ4E_read32(src, *srcIter)));
		}

		/* Catch up */
		while ((srcPos > anchorPos) && (matchPos > 0)) {
			LZ4E_rollback1(src, srcIter, &srcPos);
			LZ4E_rollback1(src, &matchIter, &matchPos);

			if (likely(LZ4E_read8(src, *srcIter)
				!= LZ4E_read8(src, matchIter))) {
				LZ4E_advance1(src, srcIter, &srcPos);
				LZ4E_advance1(src, &matchIter, &matchPos);
				break;
			}
		}

		/* Encode Literals */
		{
			const unsigned int litLength = srcPos - anchorPos;

			tokenIter = *dstIter;
			LZ4E_advance1(dst, dstIter, &dstPos);

			if ((outputLimited) &&
				/* Check output buffer overflow */
				(unlikely(dstPos + litLength +
					(2 + 1 + LASTLITERALS) +
					(litLength / 255) > maxOutputSize)))
				return 0;

			if (litLength >= RUN_MASK) {
				unsigned int len = litLength - RUN_MASK;

				token = (RUN_MASK << ML_BITS);

				for (; len >= 255; len -= 255) {
					LZ4E_write8(dst, 255, *dstIter);
					LZ4E_advance1(dst, dstIter, &dstPos);
				}
				LZ4E_write8(dst, (BYTE)len, *dstIter);
				LZ4E_advance1(dst, dstIter, &dstPos);
			} else
				token = (BYTE)(litLength << ML_BITS);

			/* Copy Literals */
			LZ4E_wildCopy(dst, src, *dstIter, anchorIter, litLength);
			LZ4E_advance(dst, dstIter, &dstPos, litLength);
		}

_next_match:
		/* Encode Offset */
		LZ4E_writeLE16(dst, (U16)(srcPos - matchPos), *dstIter);
		LZ4E_advance(dst, dstIter, &dstPos, 2);

		/* Encode MatchLength */
		{
			unsigned int matchCode;

//			if ((dict == usingExtDict)
//				&& (lowLimit == dictionary)) {
//				const BYTE *limit;
//
//				matchIter += refDelta;
//				limit = ip + (dictEnd - match);
//
//				if (limit > matchlimit)
//					limit = matchlimit;
//
//				matchCode = LZ4_count(ip + MINMATCH,
//					match + MINMATCH, limit);
//
//				ip += MINMATCH + matchCode;
//
//				if (ip == limit) {
//					unsigned const int more = LZ4_count(ip,
//						(const BYTE *)source,
//						matchlimit);
//
//					matchCode += more;
//					ip += more;
//				}
//			} else {
//
			LZ4E_advance(src, srcIter, &srcPos, MINMATCH);
			LZ4E_advance(src, &matchIter, &matchPos, MINMATCH);
			matchCode = LZ4E_count(src, *srcIter, matchIter, matchlimit - srcPos);
			LZ4E_advance(src, srcIter, &srcPos, matchCode);

			if (outputLimited &&
				/* Check output buffer overflow */
				(unlikely(dstPos +
					(1 + LASTLITERALS) +
					(matchCode >> 8) > maxOutputSize)))
				return 0;

			if (matchCode >= ML_MASK) {
				token += ML_MASK;
				matchCode -= ML_MASK;
				LZ4E_write32(dst, 0xFFFFFFFF, *dstIter);

				while (matchCode >= 4 * 255) {
					LZ4E_advance(dst, dstIter, &dstPos, 4);
					LZ4E_write32(dst, 0xFFFFFFFF, *dstIter);
					matchCode -= 4 * 255;
				}

				LZ4E_advance(dst, dstIter, &dstPos, matchCode / 255);
				LZ4E_write8(dst, (BYTE)(matchCode % 255), *dstIter);
				LZ4E_advance1(dst, dstIter, &dstPos);
			} else
				token += (BYTE)(matchCode);

			LZ4E_write8(dst, token, tokenIter);
		}

		anchorIter = *srcIter;
		anchorPos = srcPos;

		/* Test end of chunk */
		if (srcPos > mflimit)
			break;

		/* TODO:(bgch): maybe remove this */
		/* Fill table */
		LZ4E_rollback1(src, srcIter, &srcPos);
		LZ4E_rollback1(src, srcIter, &srcPos);
		LZ4E_putPosition(src, *srcIter, dictPtr->hashTable, tableType);
		LZ4E_advance(src, srcIter, &srcPos, 2);

		/* Test next position */
		matchIter = LZ4E_getPosition(src, *srcIter,
			dictPtr->hashTable, dictPtr->bvIterSize,
			tableType, srcStart);
		matchPos = LZ4E_ITER_POS(matchIter, srcStart);

//		if (dict == usingExtDict) {
//			if (match < (const BYTE *)source) {
//				refDelta = dictDelta;
//				lowLimit = dictionary;
//			} else {
//				refDelta = 0;
//				lowLimit = (const BYTE *)source;
//			}
//		}

		LZ4E_putPosition(src, *srcIter, dictPtr->hashTable, tableType);

		if ((matchPos + MAX_DISTANCE >= srcPos)
			&& (LZ4E_read32(src, *srcIter)
				== LZ4E_read32(src, matchIter))) {
			token = 0;
			tokenIter = *dstIter;
			LZ4E_advance1(dst, dstIter, &dstPos);
			goto _next_match;
		}

		/* Prepare next loop */
		LZ4E_advance1(src, srcIter, &srcPos);
		forwardH = LZ4E_hashPosition(src, *srcIter, tableType);
	}

_last_literals:
	/* Encode Last Literals */
	{
		const size_t lastRun = (size_t)(inputSize - anchorPos);

		if ((outputLimited) &&
			/* Check output buffer overflow */
			(dstPos + lastRun + 1 +
			((lastRun + 255 - RUN_MASK) / 255) > (U32)maxOutputSize))
			return 0;

		if (lastRun >= RUN_MASK) {
			size_t accumulator = lastRun - RUN_MASK;

			LZ4E_write8(dst, RUN_MASK << ML_BITS, *dstIter);
			LZ4E_advance1(dst, dstIter, &dstPos);

			for (; accumulator >= 255; accumulator -= 255) {
				LZ4E_write8(dst, 255, *dstIter);
				LZ4E_advance1(dst, dstIter, &dstPos);
			}
			LZ4E_write8(dst, (BYTE)accumulator, *dstIter);
			LZ4E_advance1(dst, dstIter, &dstPos);
		} else {
			LZ4E_write8(dst, (BYTE)(lastRun << ML_BITS), *dstIter);
			LZ4E_advance1(dst, dstIter, &dstPos);
		}

		LZ4E_memcpy(dst, src, *dstIter, anchorIter, lastRun);
		dstPos += lastRun;
	}

	/* End */
	return (int)dstPos;
}

static int LZ4E_compress_fast_extState(
	void *state,
	const struct bio_vec *src,
	struct bio_vec *dst,
	struct bvec_iter *srcIter,
	struct bvec_iter *dstIter,
	int acceleration)
{
	LZ4E_stream_t_internal *ctx = &((LZ4E_stream_t *)state)->internal_donotuse;
	const unsigned int inputSize = srcIter->bi_size;
	const unsigned int maxOutputSize = dstIter->bi_size;
	const tableType_t tableType = byU32;

	memset(state, 0, sizeof(LZ4E_stream_t));

	if (acceleration < 1)
		acceleration = LZ4_ACCELERATION_DEFAULT;

	if (maxOutputSize >= LZ4_COMPRESSBOUND(inputSize)) {
//		if (inputSize < LZ4_64Klimit)
//			return LZ4E_compress_generic(ctx,
//				src, dst, srcIter, dstIter,
//				noLimit, byU16, noDict,
//				noDictIssue, (U32)acceleration);
//		else
			return LZ4E_compress_generic(ctx,
				src, dst, srcIter, dstIter,
				noLimit, tableType, noDict,
				noDictIssue, (U32)acceleration);
	} else {
//		if (inputSize < LZ4_64Klimit)
//			return LZ4E_compress_generic(ctx,
//				src, dst, srcIter, dstIter,
//				limitedOutput, byU16, noDict,
//				noDictIssue, (U32)acceleration);
//		else
			return LZ4E_compress_generic(ctx,
				src, dst, srcIter, dstIter,
				limitedOutput, tableType, noDict,
				noDictIssue, (U32)acceleration);
	}
}

int LZ4E_compress_default(const struct bio_vec *src, struct bio_vec *dst,
	struct bvec_iter *srcIter, struct bvec_iter *dstIter, void *wrkmem)
{
	return LZ4E_compress_fast_extState(wrkmem, src, dst, srcIter,
		dstIter, LZ4_ACCELERATION_DEFAULT);
}
