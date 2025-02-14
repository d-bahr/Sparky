#ifndef REPETITION_H_
#define REPETITION_H_

#include "Intrinsics.h"
#include "StaticAssert.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TRANSPOSITION_TABLE_SIZE 256

// This should be large enough to fit a few hash collisions, without blowing up the cache size a bunch.
// A good number is something that allows a full bucket to fit in a cache line.
#define REPETITION_TABLE_BUCKET_SIZE 5

// Extra linked-list node used only when a bucket is fully used up.
// This is purely needed because we mess around with optimization tricks a lot here.
// This wouldn't be necessary if we did a pure stack-based implementation.
struct RepetitionNodeS;

typedef struct RepetitionNodeS
{
    uint64_t hash;
    struct RepetitionNodeS * next;
    struct RepetitionNodeS * prev;
} RepetitionNode;

#pragma pack(push, 1)
typedef struct
{
    uint64_t hashStack[REPETITION_TABLE_BUCKET_SIZE];
    RepetitionNode * hashListStart;
    RepetitionNode * hashListEnd;
    uint8_t stackLength;
    char padding[7];
} RepetitionBucket;
#pragma pack(pop)

STATIC_ASSERT(sizeof(RepetitionBucket) <= 64, "RepetitionBucket struct is too large for a single cache line");

typedef struct
{
    RepetitionBucket * buckets;
    // Store value minus one to make lookup and insertion one instruction faster, at expense of making clearing one instruction slower.
    // Lookup and insertion is MUCH more common, so this is a fair tradeoff.
    size_t numBucketsMinusOne;
} RepetitionTable;

static inline void RepetitionTableFreeHeapNodes(RepetitionTable * rt);

static inline bool RepetitionTableInitialize(RepetitionTable * rt, size_t numBuckets)
{
    // Number of buckets must be at least 2^16 and must be a power of 2.
    // This means the minimum repetition table size is 4MB (2^16 * sizeof(TranspositionBucket)).
    if (numBuckets < 0x10000 || intrinsic_popcnt64(numBuckets) != 1)
        return false;

    rt->buckets = (RepetitionBucket *) malloc(numBuckets * sizeof(RepetitionBucket));
    if (rt->buckets == NULL)
        return false;
    rt->numBucketsMinusOne = numBuckets - 1;
    // TODO: Parallelize w/ threads? Stockfish parallelizes this operation.
    memset(rt->buckets, 0, (rt->numBucketsMinusOne + 1) * sizeof(RepetitionBucket));
    return true;
}

// Avoid calling this. This can be slow because it needs to traverse all buckets to clean up heap memory.
static inline void RepetitionTableClear(RepetitionTable * rt)
{
    RepetitionTableFreeHeapNodes(rt);
    memset(rt->buckets, 0, (rt->numBucketsMinusOne + 1) * sizeof(RepetitionBucket));
}

static inline void RepetitionTableDestroy(RepetitionTable * rt)
{
    RepetitionTableFreeHeapNodes(rt);
    free(rt->buckets);
}

static inline void RepetitionTableFreeHeapNodes(RepetitionTable * rt)
{
    for (size_t i = 0; i <= rt->numBucketsMinusOne; ++i)
    {
        RepetitionNode * node = rt->buckets[i].hashListStart;
        while (node != NULL)
        {
            RepetitionNode * thisNode = node;
            node = node->next;
            free(thisNode);
        }
    }
}

static inline bool RepetitionTableContains(RepetitionTable * rt, uint64_t hash)
{
    RepetitionBucket * bucket = &rt->buckets[hash & rt->numBucketsMinusOne];
    // Check the stack first. Since this is all in one cache line, this should be pretty fast for 99% of cases.
    for (int i = 0; i < bucket->stackLength; ++i)
    {
        if (hash == bucket->hashStack[i])
            return true;
    }
    // Check the linked list extension if present.
    RepetitionNode * node = bucket->hashListStart;
    while (node != NULL)
    {
        if (node->hash == hash)
            return true;
        node = node->next;
    }
    return false;
}

static inline void RepetitionTablePush(RepetitionTable * rt, uint64_t hash)
{
    RepetitionBucket * bucket = &rt->buckets[hash & rt->numBucketsMinusOne];
    if (bucket->stackLength < REPETITION_TABLE_BUCKET_SIZE)
    {
        bucket->hashStack[bucket->stackLength] = hash;
        bucket->stackLength++;
    }
    else
    {
        if (bucket->hashListStart == NULL)
        {
            bucket->hashListStart = (RepetitionNode *) malloc(sizeof(RepetitionNode));
            if (bucket->hashListStart != NULL)
            {
                bucket->hashListStart->hash = hash;
                bucket->hashListStart->next = NULL;
                bucket->hashListStart->prev = NULL;
                bucket->hashListEnd = bucket->hashListStart;
            }
        }
        else
        {
            assert(bucket->hashListEnd != NULL);
            assert(bucket->hashListEnd->next == NULL);

            RepetitionNode * newNode = (RepetitionNode *) malloc(sizeof(RepetitionNode));
            if (newNode != NULL)
            {
                newNode->hash = hash;
                newNode->next = NULL;
                newNode->prev = bucket->hashListEnd;
                bucket->hashListEnd->next = newNode;
                bucket->hashListEnd = newNode;
            }
        }
    }
}

static inline void RepetitionTablePop(RepetitionTable * rt, uint64_t hash)
{
    RepetitionBucket * bucket = &rt->buckets[hash & rt->numBucketsMinusOne];
    if (bucket->hashListStart == NULL)
    {
        if (bucket->stackLength > 0)
            bucket->stackLength--;
    }
    else
    {
        assert(bucket->hashListEnd != NULL);

        if (bucket->hashListStart == bucket->hashListEnd)
        {
            free(bucket->hashListStart);
            bucket->hashListStart = NULL;
            bucket->hashListEnd = NULL;
        }
        else
        {
            RepetitionNode * nodeToFree = bucket->hashListEnd;
            bucket->hashListEnd = nodeToFree->prev;
            bucket->hashListEnd->next = NULL;

            free(nodeToFree);
        }
    }
}

#endif // REPETITION_H_
