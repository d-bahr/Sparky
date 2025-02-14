#include "Board.h"
#include "Evaluation.h"
#include "FEN.h"
#include "KillerMove.h"
#include "Init.h"
#include "Move.h"
#include "MoveGeneration.h"
#include "MoveOrderer.h"
#include "Piece.h"
#include "PieceType.h"
#include "Player.h"
#include "Square.h"
#include "Repetition.h"
#include "Sort.h"
#include "Square.h"
#include "StaticEval.h"
#include "Syzygy.h"
#include "ThreadPool.h"
#include "Transposition.h"
#include "Zobrist.h"

#include <stdio.h>

static bool s_fail = false;

void PrintError(const char * err, int line)
{
    printf(err, line);
    s_fail = true;
}

#define ERR_MSG(msg) PrintError(msg " (" __FILE__ ": %d)\n", __LINE__)
#define ERR_MSG_1(msg, _1) printf(msg " (" __FILE__ ": %d)\n", (_1), __LINE__)
#define ERR_MSG_2(msg, _1, _2) printf(msg " (" __FILE__ ": %d)\n", (_1), (_2), __LINE__)

#define ASSERT_COMMON(a, op, b) \
    do \
    { \
        if (!((a) op (b))) \
        { \
            ERR_MSG("Assertion failed: " #a " " #op " " #b); \
            return; \
        } \
    } \
    while (0)

#define ASSERT_EQ(a, b) ASSERT_COMMON(a, ==, b)
#define ASSERT_NE(a, b) ASSERT_COMMON(a, !=, b)
#define ASSERT_GT(a, b) ASSERT_COMMON(a, >, b)
#define ASSERT_GE(a, b) ASSERT_COMMON(a, >=, b)
#define ASSERT_LT(a, b) ASSERT_COMMON(a, <, b)
#define ASSERT_LE(a, b) ASSERT_COMMON(a, <=, b)
#define ASSERT_TRUE(a) ASSERT_COMMON(a, ==, true)
#define ASSERT_FALSE(a) ASSERT_COMMON(a, ==, false)

#define EXPECT_COMMON(a, op, b) \
    do \
    { \
        if (!((a) op (b))) \
        { \
            ERR_MSG("Assertion failed: " #a " " #op " " #b); \
        } \
    } \
    while (0)

#define EXPECT_EQ(a, b) EXPECT_COMMON(a, ==, b)
#define EXPECT_NE(a, b) EXPECT_COMMON(a, !=, b)
#define EXPECT_GT(a, b) EXPECT_COMMON(a, >, b)
#define EXPECT_GE(a, b) EXPECT_COMMON(a, >=, b)
#define EXPECT_LT(a, b) EXPECT_COMMON(a, <, b)
#define EXPECT_LE(a, b) EXPECT_COMMON(a, <=, b)
#define EXPECT_TRUE(a) EXPECT_COMMON(a, ==, true)
#define EXPECT_FALSE(a) EXPECT_COMMON(a, ==, false)

#define FAIL_SOFT() do { ERR_MSG("Failure"); } while(0)
#define FAIL_HARD() do { ERR_MSG("Failure"); return; } while(0)

void TestMoves()
{
    for (int p = 0; p < NUM_PIECE_TYPES; ++p)
    {
        for (int r = 0; r < NUM_RANKS; ++r)
        {
            for (int f = 0; f < NUM_FILES; ++f)
            {
                Move m = { 0 };
                m.from = SquareFromRankFile(r, f);
                m.to = SquareFromRankFile((r + 1) % NUM_RANKS, (f + 1) % NUM_FILES);
                m.promotion = p;
                EncodedMove encoded = MoveEncode(m);
                Move decoded = MoveDecode(encoded);
                EXPECT_EQ(m.from, decoded.from);
                EXPECT_EQ(m.to, decoded.to);
                EXPECT_EQ(m.promotion, decoded.promotion);
                EXPECT_TRUE(MoveEquals(m, decoded));
                EXPECT_TRUE(EncodedMoveValid(encoded));
            }
        }
    }
}

void TestTransposition()
{
    TranspositionTable table;
    ASSERT_FALSE(TranspositionTableInitialize(&table, 1));
    ASSERT_FALSE(TranspositionTableInitialize(&table, 0x8000));
    ASSERT_FALSE(TranspositionTableInitialize(&table, 0xFFFF));
    ASSERT_FALSE(TranspositionTableInitialize(&table, 0x10001));
    ASSERT_TRUE(TranspositionTableInitialize(&table, 0x10000));
    
    EXPECT_EQ(table.numBucketsMinusOne, 0x10000 - 1);
    EXPECT_EQ(table.utilization, 0u);
    ASSERT_NE(table.buckets, NULL);
    EXPECT_EQ(TranspositionTableGetUtilization(&table), 0u);

    EncodedMove encodedMoveLookup = 0;
    int32_t evalLookup = 0;
    int32_t depthLookup = 0;
    TranspositionType typeLookup = TranspositionNone;
    
    for (uint64_t hash = 1; hash < 0x800000; hash += 0x10001) // Start at 1; a hash of zero is invalid.
    {
        for (int p = 0; p < NUM_PIECE_TYPES; ++p)
        {
            for (int r = 0; r < NUM_RANKS; ++r)
            {
                for (int f = 0; f < NUM_FILES; ++f)
                {
                    for (int32_t eval = -10000; eval < 10000; eval += 97)
                    {
                        for (int32_t depth = 0; depth < 10; ++depth)
                        {
                            for (TranspositionType type = TranspositionNone; type <= TranspositionExact; ++type)
                            {
                                Move m = { 0 };
                                m.from = SquareFromRankFile(r, f);
                                m.to = SquareFromRankFile((r + 1) % NUM_RANKS, (f + 1) % NUM_FILES);
                                m.promotion = p;
                                EncodedMove encoded = MoveEncode(m);
                                const TranspositionBucket * bucket = &table.buckets[hash & table.numBucketsMinusOne];
                                ASSERT_NE(bucket, NULL);
                                bool inserted = true;
                                for (uint8_t i = 0; i < bucket->length; ++i)
                                {
                                    if ((bucket->transpositions[i].hashAndMove & 0xFFFFFFFFFFFF0000ul) == (hash & 0xFFFFFFFFFFFF0000ul))
                                    {
                                        const Transposition * t = &bucket->transpositions[i];
                                        if (TranspositionTableReadDepth(t) >= depth)
                                            inserted = false;
                                        break;
                                    }
                                }
                                TranspositionTableInsert(&table, hash, encoded, eval, depth, type);
                                EXPECT_GE(bucket->length, 1u);
                                bool found = false;
                                for (uint8_t i = 0; i < bucket->length; ++i)
                                {
                                    if ((bucket->transpositions[i].hashAndMove & 0xFFFFFFFFFFFF0000ul) == (hash & 0xFFFFFFFFFFFF0000ul))
                                    {
                                        const Transposition * t = &bucket->transpositions[i];
                                        EXPECT_GE(TranspositionTableReadDepth(t), depth);
                                        EXPECT_GE(t->depth, (uint32_t)(depth + TT_DEPTH_OFFSET));
                                        if (inserted)
                                        {
                                            EXPECT_EQ(TranspositionTableReadDepth(t), depth);
                                            EXPECT_EQ(t->depth, depth + TT_DEPTH_OFFSET);
                                            EXPECT_EQ(t->hashAndMove, (hash & 0xFFFFFFFFFFFF0000ull) | encoded);
                                            EXPECT_EQ(t->evaluation, eval + TT_EVAL_OFFSET);
                                            EXPECT_EQ(TranspositionTableReadEval(t), eval);
                                            EXPECT_EQ(t->type, type);
                                            typeLookup = TranspositionTableLookup(&table, hash, &encodedMoveLookup, &evalLookup, &depthLookup);
                                            EXPECT_EQ(typeLookup, type);
                                            EXPECT_EQ(encoded, encodedMoveLookup);
                                            EXPECT_EQ(evalLookup, eval);
                                            EXPECT_EQ(depthLookup, depth);
                                        }
                                        found = true;
                                        break;
                                    }
                                }
                                EXPECT_TRUE(found);
                            }
                        }
                    }
                }
            }
        }
    }

    TranspositionTableClear(&table);
    EXPECT_EQ(table.utilization, 0u);
    for (size_t i = 0; i <= table.numBucketsMinusOne; ++i)
        EXPECT_EQ(table.buckets[i].length, 0u);
    EXPECT_EQ(TranspositionTableGetUtilization(&table), 0u);
    
    TranspositionTableDestroy(&table);
}

void TestRepetition()
{
    RepetitionTable table;
    ASSERT_FALSE(RepetitionTableInitialize(&table, 1));
    ASSERT_FALSE(RepetitionTableInitialize(&table, 0x8000));
    ASSERT_FALSE(RepetitionTableInitialize(&table, 0xFFFF));
    ASSERT_FALSE(RepetitionTableInitialize(&table, 0x10001));
    ASSERT_TRUE(RepetitionTableInitialize(&table, 0x10000));

    EXPECT_EQ(table.numBucketsMinusOne, 0x10000 - 1);
    ASSERT_NE(table.buckets, NULL);

    // Enough loop iterations to go through all possible hash buckets a couple of times.
    uint64_t increment = (table.numBucketsMinusOne + 2);
    uint64_t fullIteration = (table.numBucketsMinusOne + 1) * increment;
    uint64_t end = fullIteration * (REPETITION_TABLE_BUCKET_SIZE + 2);
    uint64_t hash = 0;
    for (; hash < end; hash += increment)
    {
        uint64_t expectedBucketLength = (hash / fullIteration) + 1;
        RepetitionTablePush(&table, hash);
        const RepetitionBucket * bucket = &table.buckets[hash & table.numBucketsMinusOne];

        if (expectedBucketLength > REPETITION_TABLE_BUCKET_SIZE)
        {
            EXPECT_EQ(bucket->stackLength, REPETITION_TABLE_BUCKET_SIZE);
            ASSERT_NE(bucket->hashListStart, NULL);
            ASSERT_NE(bucket->hashListEnd, NULL);
            EXPECT_EQ(bucket->hashListEnd->hash, hash);
        }
        else
        {
            EXPECT_EQ(bucket->stackLength, expectedBucketLength);
            EXPECT_EQ(hash, bucket->hashStack[expectedBucketLength - 1]);
        }
        EXPECT_TRUE(RepetitionTableContains(&table, hash));
        EXPECT_FALSE(RepetitionTableContains(&table, hash + increment));
    }
    hash -= increment; // This hash never actually got added because of the loop termination after increment, so account for it here.
    for (; hash >= increment; hash -= increment)
    {
        uint64_t expectedBucketLength = hash / fullIteration;
        RepetitionTablePop(&table, hash);
        const RepetitionBucket * bucket = &table.buckets[hash & table.numBucketsMinusOne];

        if (expectedBucketLength > REPETITION_TABLE_BUCKET_SIZE)
        {
            EXPECT_EQ(bucket->stackLength, REPETITION_TABLE_BUCKET_SIZE);
            ASSERT_NE(bucket->hashListStart, NULL);
            ASSERT_NE(bucket->hashListEnd, NULL);
        }
        else
        {
            EXPECT_EQ(bucket->stackLength, expectedBucketLength);
        }
        EXPECT_FALSE(RepetitionTableContains(&table, hash));
        EXPECT_TRUE(RepetitionTableContains(&table, hash - increment));
    }
    EXPECT_TRUE(RepetitionTableContains(&table, 0));
    RepetitionTablePop(&table, 0);
    EXPECT_FALSE(RepetitionTableContains(&table, 0));

    RepetitionTablePush(&table, 0);
    RepetitionTableClear(&table);
    EXPECT_FALSE(RepetitionTableContains(&table, 0));

    RepetitionTableDestroy(&table);
}

static void CompareCharArrays(const char * a, size_t aLen, const char * b, size_t bLen)
{
    ASSERT_EQ(aLen, bLen);
    for (size_t i = 0; i < aLen; ++i)
        EXPECT_EQ(a[i], b[i]);
}

static void CompareIntArrays(const int * a, size_t aLen, const int * b, size_t bLen)
{
    ASSERT_EQ(aLen, bLen);
    for (size_t i = 0; i < aLen; ++i)
        EXPECT_EQ(a[i], b[i]);
}

static bool SortIntGreater(void * a, void * b)
{
    return (*(int *) a) > (*(int *) b);
}

static bool SortChar(void * a, void * b)
{
    return (*(char *) a) < (*(char *) b);
}

static void SortCharArray(char * a, size_t len)
{
    StableSort(a, a + len, sizeof(*a), SortChar);
}

static void SortIntArray(int * a, size_t len)
{
    StableSort(a, a + len, sizeof(*a), SortIntGreater);
}

void TestSort()
{
    char CharArrayA[] = { 'f', 'b', 'a', 'c', 'e', 'd' };
    char CharArrayB[] = { '1', '2', '3' };
    char CharArrayC[] = { 'Z' };
    char CharArrayD[] = { 'Z', 'Y' };
    char CharArrayE[] = { 'Y', 'Z' };

    int IntArrayA[] = { 1, 2, 3, 4, 5 };
    int IntArrayB[] = { 5, 4, 3, 2, 1 };
    int IntArrayC[] = { 1, 1, 2, 2, 3 };
    int IntArrayD[] = { 7, 10, 4, 1, 66, 10 };
    int IntArrayE[] = { 1, 5 };
    int IntArrayF[] = { 4 };

    SortCharArray(CharArrayA, sizeof(CharArrayA) / sizeof(CharArrayA[0]));
    SortCharArray(CharArrayB, sizeof(CharArrayB) / sizeof(CharArrayB[0]));
    SortCharArray(CharArrayC, sizeof(CharArrayC) / sizeof(CharArrayC[0]));
    SortCharArray(CharArrayD, sizeof(CharArrayD) / sizeof(CharArrayD[0]));
    SortCharArray(CharArrayE, sizeof(CharArrayE) / sizeof(CharArrayE[0]));

    CompareCharArrays(CharArrayA, sizeof(CharArrayA) / sizeof(CharArrayA[0]), "abcdef", 6);
    CompareCharArrays(CharArrayB, sizeof(CharArrayB) / sizeof(CharArrayB[0]), "123", 3);
    CompareCharArrays(CharArrayC, sizeof(CharArrayC) / sizeof(CharArrayC[0]), "Z", 1);
    CompareCharArrays(CharArrayD, sizeof(CharArrayD) / sizeof(CharArrayD[0]), "YZ", 2);
    CompareCharArrays(CharArrayE, sizeof(CharArrayE) / sizeof(CharArrayE[0]), "YZ", 2);

    SortIntArray(IntArrayA, sizeof(IntArrayA) / sizeof(IntArrayA[0]));
    SortIntArray(IntArrayB, sizeof(IntArrayB) / sizeof(IntArrayB[0]));
    SortIntArray(IntArrayC, sizeof(IntArrayC) / sizeof(IntArrayC[0]));
    SortIntArray(IntArrayD, sizeof(IntArrayD) / sizeof(IntArrayD[0]));
    SortIntArray(IntArrayE, sizeof(IntArrayE) / sizeof(IntArrayE[0]));
    SortIntArray(IntArrayF, sizeof(IntArrayF) / sizeof(IntArrayF[0]));

    CompareIntArrays(IntArrayA, sizeof(IntArrayA) / sizeof(IntArrayA[0]), (int[5]) { 5, 4, 3, 2, 1 }, 5);
    CompareIntArrays(IntArrayB, sizeof(IntArrayB) / sizeof(IntArrayB[0]), (int[5]) { 5, 4, 3, 2, 1 }, 5);
    CompareIntArrays(IntArrayC, sizeof(IntArrayC) / sizeof(IntArrayC[0]), (int[5]) { 3, 2, 2, 1, 1 }, 5);
    CompareIntArrays(IntArrayD, sizeof(IntArrayD) / sizeof(IntArrayD[0]), (int[6]) { 66, 10, 10, 7, 4, 1 }, 6);
    CompareIntArrays(IntArrayE, sizeof(IntArrayE) / sizeof(IntArrayE[0]), (int[2]) { 5, 1 }, 2);
    CompareIntArrays(IntArrayF, sizeof(IntArrayF) / sizeof(IntArrayF[0]), (int[1]) { 4 }, 1);

    SortCharArray(CharArrayE, 0);
    CompareCharArrays(CharArrayE, 0, NULL, 0);
    SortIntArray(IntArrayF, 0);
    CompareIntArrays(IntArrayF, 0, NULL, 0);
}

// This test is most useful with Valgrind to check for memory leaks.
void TestInit()
{
    ASSERT_EQ(Init(NULL), 0);

    ASSERT_TRUE(EvalInit(0x200000));

    ASSERT_TRUE(ThreadPoolInitialize(2));

#ifdef _WIN32
    EXPECT_TRUE(SyzygyInit(".;syzygy\\3-4-5-dtz-nr;syzygy\\3-4-5-wdl;..\\..\\syzygy\\3-4-5-dtz-nr;..\\..\\syzygy\\3-4-5-wdl"));
#else
    EXPECT_TRUE(SyzygyInit(".:syzygy/3-4-5-dtz-nr:syzygy/3-4-5-wdl:../../syzygy/3-4-5-dtz-nr:../../syzygy/3-4-5-wdl"));
#endif

    SyzygyDestroy();
    ThreadPoolDestroy();
    EvalDestroy();
    Cleanup();
}

#define NO_BOARD_STACK

// Give or take enough space for maximum number of moves for MAX_LINE_DEPTH * 2 consecutive ply, which should be enough for any reasonably conceivable position.
static Move s_moves[65536]; // Probably excessive, but whatever.

void CheckZobristRecursive(Board * board, uint64_t curDepth, uint64_t maxDepth, uint64_t mm)
{
    Move * moves = &s_moves[mm];
    Board nextBoard;

    uint64_t numMoves = GetValidMoves(board, moves);

    if (curDepth == maxDepth)
        return;

    for (uint64_t i = 0; i < numMoves; ++i)
    {
        nextBoard = *board;
        MakeMove(&nextBoard, moves[i]);

        uint64_t expected = ZobristCalculate(&nextBoard);
        EXPECT_EQ(nextBoard.hash, expected);

        CheckZobristRecursive(&nextBoard, curDepth + 1, maxDepth, mm + numMoves);
    }
}

void CheckZobrist(const char * fen, uint64_t depth)
{
    Board board;
    if (!ParseFEN(fen, &board))
    {
        printf("Invalid FEN\n");
        return;
    }

    CheckZobristRecursive(&board, 0, depth, 0);
}

void TestZobrist()
{
    Init(NULL);

    // Set up some complex positions and iterate over a large tree of moves, checking the Zobrist merge against the fully calculated value.
    // This is similar to perft but we're checking Zobrist instead of number of positions.
    CheckZobrist("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0", 4);
    CheckZobrist("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 4);
    CheckZobrist("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", 4);
    CheckZobrist("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4);
    CheckZobrist("r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 4);
    CheckZobrist("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4);
    CheckZobrist("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4);
    CheckZobrist("rnb1kbnr/pp1pp1pp/1qp2p2/8/Q1P5/N7/PP1PPPPP/1RB1KBNR b Kkq - 2 4", 4);

    Cleanup();
}

int main(int argc, char ** argv)
{
    ZobristGenerate();

    TestMoves();
    TestTransposition();
    TestRepetition();
    TestSort();
    TestInit();
    TestZobrist();
    if (s_fail)
        printf("Unit tests failed.\n");
    return s_fail ? 1 : 0;
}
