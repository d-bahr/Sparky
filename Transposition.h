#ifndef TRANSPOSITION_H_
#define TRANSPOSITION_H_

#include "Intrinsics.h"
#include "Move.h"
#include "StaticAssert.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TT_EVAL_OFFSET 0x200000
#define TT_DEPTH_OFFSET 0x80

typedef enum
{
    TranspositionNone,
    TranspositionAlpha,
    TranspositionBeta,
    TranspositionExact // Note that: TranspositionExact == (TranspositionAlpha | TranspositionBeta)
} TranspositionType;

// 16 bytes.
#pragma pack(push, 1)
typedef struct
{
    // [0:15]: EncodedMove
    // [16:63]: Upper 48 bits of hash.
    // This is handled manually instead of using bitfields because we can optimize it slightly in TranspositionTableLookup().
    uint64_t hashAndMove;
    uint32_t evaluation : 22; // Maximum non-mate eval is somewhere around 100-110 pawns based purely on piece value. +-256 should be enough overhead.
    uint32_t depth : 8; // Must be able to store a number that is at least MAX_LINE_DEPTH, plus extra for quiescence.
    uint32_t type : 2;
    
} Transposition;
#pragma pack(pop)

STATIC_ASSERT(sizeof(Transposition) <= 16, "Unexpected size of Transposition struct");

#define TRANSPOSITION_TABLE_BUCKET_SIZE 5

// This should fit in a single cache line for best performance.
// Most processors (as of 2024) have 64 byte cache lines, and some Apple M-series processors have 128 byte cache lines.
// 64 bytes is a reasonable limit here.
#pragma pack(push, 1)
typedef struct
{
    Transposition transpositions[TRANSPOSITION_TABLE_BUCKET_SIZE];
    uint8_t length;
    char padding[3];
} TranspositionBucket;
#pragma pack(pop)

STATIC_ASSERT(sizeof(TranspositionBucket) <= 64, "TranspositionBucket struct is too large for a single cache line");

typedef struct
{
    TranspositionBucket * buckets;
    // Store value minus one to make lookup and insertion one instruction faster, at expense of making clearing one instruction slower.
    // Lookup and insertion is MUCH more common, so this is a fair tradeoff.
    size_t numBucketsMinusOne;
    uint64_t utilization;
} TranspositionTable;

static inline void TranspositionTableClear(TranspositionTable * tt);

static inline bool TranspositionTableInitialize(TranspositionTable * tt, size_t numBuckets)
{
    // Number of buckets must be at least 2^16 and must be a power of 2.
    // This means the minimum transposition table size is 4MB (2^16 * sizeof(TranspositionBucket)).
    if (numBuckets < 0x10000 || intrinsic_popcnt64(numBuckets) != 1)
        return false;

    tt->buckets = (TranspositionBucket *)malloc(numBuckets * sizeof(TranspositionBucket));
    if (tt->buckets == NULL)
        return false;
    tt->numBucketsMinusOne = numBuckets - 1;
    tt->utilization = 0;
    TranspositionTableClear(tt);
    return true;
}

static inline void TranspositionTableDestroy(TranspositionTable * tt)
{
    free(tt->buckets);
}

static inline void TranspositionTableClear(TranspositionTable * tt)
{
    // TODO: Parallelize w/ threads? Stockfish parallelizes this operation.
    memset(tt->buckets, 0, (tt->numBucketsMinusOne + 1) * sizeof(TranspositionBucket));
    tt->utilization = 0;
}

static inline bool TranspositionTableResize(TranspositionTable * tt, size_t numBuckets)
{
    // Note: resizing causes the contents of the table to be obliterated.
    TranspositionTableDestroy(tt);
    return TranspositionTableInitialize(tt, numBuckets);
}

static FORCE_INLINE void TranspositionTableStoreEval(Transposition * t, int32_t evaluation)
{
    assert(evaluation >= -TT_EVAL_OFFSET && evaluation < TT_EVAL_OFFSET);
    t->evaluation = (uint32_t) (evaluation + TT_EVAL_OFFSET);
}

static FORCE_INLINE int32_t TranspositionTableReadEval(const Transposition * t)
{
    return ((int32_t) t->evaluation) - TT_EVAL_OFFSET;
}

static FORCE_INLINE void TranspositionTableStoreDepth(Transposition * t, int32_t depth)
{
    assert(depth >= -TT_DEPTH_OFFSET && depth < TT_DEPTH_OFFSET);
    t->depth = (uint32_t) (depth + TT_DEPTH_OFFSET);
}

static FORCE_INLINE int32_t TranspositionTableReadDepth(const Transposition * t)
{
    return ((int32_t) t->depth) - TT_DEPTH_OFFSET;
}

static inline TranspositionType TranspositionTableLookup(TranspositionTable * tt, uint64_t hash, EncodedMove * move, int32_t * eval, int32_t * depth)
{
    assert(hash != 0);
    TranspositionBucket * bucket = &tt->buckets[hash & tt->numBucketsMinusOne];
    for (int i = 0; i < bucket->length; ++i)
    {
        Transposition * transposition = &bucket->transpositions[i];
        if ((hash & 0xFFFFFFFFFFFF0000ull) == (transposition->hashAndMove & 0xFFFFFFFFFFFF0000ull))
        {
            // Shuffle to put most recent transposition first (LRU cache).
            /*if (i > 0)
            {
                Transposition temp = *transposition;
                memmove(&bucket->transpositions[1], &bucket->transpositions[0], i * sizeof(Transposition));
                bucket->transpositions[0] = temp;
            }*/

            *depth = TranspositionTableReadDepth(transposition);
            *eval = TranspositionTableReadEval(transposition);
            *move = transposition->hashAndMove & 0xFFFF;
            return transposition->type;
        }
    }
    return TranspositionNone;
}

static inline void TranspositionTableInsert(TranspositionTable * tt, uint64_t hash, EncodedMove move, int32_t evaluation, int32_t depth, TranspositionType type)
{
    assert(hash != 0);
    TranspositionBucket * bucket = &tt->buckets[hash & tt->numBucketsMinusOne];
    Transposition * transposition = NULL;

    for (int i = 0; i < bucket->length; ++i)
    {
        transposition = &bucket->transpositions[i];
        if ((hash & 0xFFFFFFFFFFFF0000ull) == (transposition->hashAndMove & 0xFFFFFFFFFFFF0000ull))
        {
            if (depth > TranspositionTableReadDepth(transposition))
            {
                // Replace existing hash.
                transposition->hashAndMove = (hash & 0xFFFFFFFFFFFF0000ull) | move;
                TranspositionTableStoreEval(transposition, evaluation);
                TranspositionTableStoreDepth(transposition, depth);
                transposition->type = type;
            }
            return;
        }
    }

    if (bucket->length < TRANSPOSITION_TABLE_BUCKET_SIZE)
    {
        bucket->transpositions[bucket->length].hashAndMove = (hash & 0xFFFFFFFFFFFF0000ull) | move;
        TranspositionTableStoreEval(&bucket->transpositions[bucket->length], evaluation);
        TranspositionTableStoreDepth(&bucket->transpositions[bucket->length], depth);
        bucket->transpositions[bucket->length].type = type;
        bucket->length++;
        tt->utilization++;
    }
    else
    {
        transposition = &bucket->transpositions[0];
        for (int i = 1; i < bucket->length; ++i)
        {
            if (bucket->transpositions[i].depth < transposition->depth)
                transposition = &bucket->transpositions[i];
        }

        // Replace at the lowest depth.
        transposition->hashAndMove = (hash & 0xFFFFFFFFFFFF0000ull) | move;
        TranspositionTableStoreEval(transposition, evaluation);
        TranspositionTableStoreDepth(transposition, depth);
        transposition->type = type;
    }

    // Shuffle down to make room for newest entry. Least-recently-update entry is removed.
    /*if (bucket->length < TRANSPOSITION_TABLE_BUCKET_SIZE)
    {
        memmove(&bucket->transpositions[1], &bucket->transpositions[0], bucket->length * sizeof(Transposition));
        bucket->transpositions[0].hashAndMove = (hash & 0xFFFFFFFFFFFF0000ull) | move;
        TranspositionTableStoreEval(&bucket->transpositions[0], evaluation);
        TranspositionTableStoreDepth(&bucket->transpositions[0], depth);
        bucket->transpositions[0].type = type;
        bucket->length++;
        tt->utilization++;
    }
    else
    {
        memmove(&bucket->transpositions[1], &bucket->transpositions[0], (TRANSPOSITION_TABLE_BUCKET_SIZE - 1) * sizeof(Transposition));
        bucket->transpositions[0].hashAndMove = (hash & 0xFFFFFFFFFFFF0000ull) | move;
        TranspositionTableStoreEval(&bucket->transpositions[0], evaluation);
        TranspositionTableStoreDepth(&bucket->transpositions[0], depth);
        bucket->transpositions[0].type = type;
    }*/
}

static inline uint64_t TranspositionTableGetUtilization(const TranspositionTable * tt)
{
    return (tt->utilization * 1000) / (((uint64_t) tt->numBucketsMinusOne + 1) * TRANSPOSITION_TABLE_BUCKET_SIZE);
}

static inline size_t TranspositionTableConvertNumBuckets(size_t sizeMB)
{
    if (sizeMB == 0)
        return 0;

    // Converts hash size in MB to a bucket quantity.
    // Round up to the nearest power of 2 so that it works with TranspositionTableInitialize.
    size_t k = (sizeMB * 1024 * 1024) / sizeof(TranspositionBucket);
    if (intrinsic_popcnt64(k) == 1)
        return 1ull << intrinsic_bsr64(k); // Exact power of 2 already.
    else
        return 1ull << (intrinsic_bsr64(k) + 1ull); // Round up to the next power of 2.
}

static inline size_t TranspositionTableConvertSize(size_t numBuckets)
{
    return (numBuckets * 64u) / (1024u * 1024u);
}

#endif // TRANSPOSITION_H_
