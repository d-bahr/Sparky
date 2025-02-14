#include "Board.h"
#include "FEN.h"
#include "Init.h"
#include "Move.h"
#include "MoveGeneration.h"
#include "Player.h"

#ifdef __GNUC__
#include <alloca.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#define PSEUDO_LEGAL 1

// Give or take enough space for maximum number of moves for MAX_LINE_DEPTH * 2 consecutive ply, which should be enough for any reasonably conceivable position.
static Move s_moves[65536]; // Probably excessive, but whatever.

void PrintBoard(const Board * b)
{
    for (int i = 0; i < NUM_PIECE_TABLES; ++i)
        printf("0x%016" PRIx64 "\n", b->whitePieceTables[i]);
    for (int i = 0; i < NUM_PIECE_TABLES; ++i)
        printf("0x%016" PRIx64 "\n", b->blackPieceTables[i]);
    printf("%" PRIu8 "\n", b->whiteKingSquare);
    printf("%" PRIu8 "\n", b->blackKingSquare);
    printf("%" PRIu64 "\n", b->allPieceTables);
    printf("%" PRIu8 "\n", b->enPassantSquare);
}

#define NO_BOARD_STACK

#ifdef MAKE_UNMAKE_MOVE
static uint64_t CountMoves(Board * board, uint64_t curDepth, uint64_t maxDepth, uint64_t mm, uint64_t * moveCounters, uint64_t * checkmates)
#elif defined(NO_BOARD_STACK)
static uint64_t CountMoves(Board * board, uint64_t curDepth, uint64_t maxDepth, uint64_t mm, uint64_t * moveCounters, uint64_t * checkmates)
#else
static uint64_t CountMoves(BoardStack * boardStack, uint64_t curDepth, uint64_t maxDepth, uint64_t mm, uint64_t * moveCounters, uint64_t * checkmates)
#endif
{
    Move * moves = &s_moves[mm];
#ifdef MAKE_UNMAKE_MOVE
    MakeUnmakeState makeUnmakeState;
#elif defined(NO_BOARD_STACK)
    Board nextBoard;
#else
    Board * board = BoardStackTop(boardStack);
#endif

#if PSEUDO_LEGAL
    if (curDepth == maxDepth)
    {
        uint64_t numMoves = GetValidMoves(board, moves);

        moveCounters[curDepth] += numMoves;

        if (numMoves == 0)
            checkmates[curDepth]++;

        return numMoves;
    }
    
    uint64_t numMoves = GetPseudoLegalMoves(board, moves);
#else
    uint64_t numMoves = GetValidMoves(board, moves);

    moveCounters[curDepth] += numMoves;

    if (numMoves == 0)
        checkmates[curDepth]++;

    if (curDepth == maxDepth)
        return numMoves;
#endif

    uint64_t movesAtLeaves = 0;
    uint64_t numValidMoves = 0;

    for (uint64_t i = 0; i < numMoves; ++i)
    {
#if PSEUDO_LEGAL
        if (!IsMoveValid(board, moves[i]))
            continue;
#endif

        numValidMoves++;

#ifdef MAKE_UNMAKE_MOVE
        MakeMove(board, moves[i], &makeUnmakeState);
        uint64_t z = CountMoves(board, curDepth + 1, maxDepth, mm + numMoves, moveCounters, checkmates);
#elif defined(NO_BOARD_STACK)
        nextBoard = *board;
        MakeMove(&nextBoard, moves[i]);
        uint64_t z = CountMoves(&nextBoard, curDepth + 1, maxDepth, mm + numMoves, moveCounters, checkmates);
#else
        board = BoardStackPushNewCopy(boardStack);
        MakeMove(board, moves[i]);
        uint64_t z = CountMoves(boardStack, curDepth + 1, maxDepth, mm + numMoves, moveCounters, checkmates);
#endif
        if (curDepth == 0)
            printf("%c%c%c%c: %" PRIu64 "\n", SquareGetFile(moves[i].from) + 'a', SquareGetRank(moves[i].from) + '1', SquareGetFile(moves[i].to) + 'a', SquareGetRank(moves[i].to) + '1', z);
        movesAtLeaves += z;
#ifdef MAKE_UNMAKE_MOVE
        UnmakeMove(board, moves[i], &makeUnmakeState);
#elif !defined(NO_BOARD_STACK)
        BoardStackPop(boardStack);
#endif
    }

#if PSEUDO_LEGAL
    moveCounters[curDepth] += numValidMoves;

    /*Move zzz[222];
    int hg = GetValidMoves(board, playerToMove, &zzz);
    if (numValidMoves != hg)
    {
        char ccc[256];
        memset(ccc, 0, sizeof(ccc));
        ToFEN(board, playerToMove, ccc, sizeof(ccc) - 1);
        puts(ccc);
        for (uint64_t i = 0; i < numMoves; ++i)
        {
#if PSEUDO_LEGAL
            if (IsMoveValid(board, playerToMove, moves[i]))
                printf("Valid: %i\n", i);
#endif
        }
         printf("UHOH!\n");
    }*/

    if (numValidMoves == 0)
        checkmates[curDepth]++;
#endif

    return movesAtLeaves;
}

bool perft(const char * fen, uint64_t depth, const uint64_t * compare)
{
    printf("Starting perft on position %s\n", fen);

#if defined(MAKE_UNMAKE_MOVE) || defined(NO_BOARD_STACK)
    Board board;
    if (!ParseFEN(fen, &board))
    {
        printf("Invalid FEN\n");
        return false;
    }
#else
    BoardStack boardStack;
    BoardStackInitialize(&boardStack);
    Board * board = BoardStackPushNew(&boardStack);
    if (!ParseFEN(fen, board))
    {
        BoardStackDestroy(&boardStack);
        printf("Invalid FEN\n");
        return false;
    }
#endif

    uint64_t * numMoves = (uint64_t *) alloca(sizeof(uint64_t) * depth);
    uint64_t * numCheckmates = (uint64_t *) alloca(sizeof(uint64_t) * depth);
    memset(numMoves, 0, sizeof(uint64_t) * depth);
    memset(numCheckmates, 0, sizeof(uint64_t) * depth);

    clock_t begin;
    clock_t end;

    begin = clock();

#ifdef MAKE_UNMAKE_MOVE
    CountMoves(&board, 0, depth - 1, 0, numMoves, numCheckmates);
#elif defined(NO_BOARD_STACK)
    CountMoves(&board, 0, depth - 1, 0, numMoves, numCheckmates);
#else
    CountMoves(&boardStack, 0, depth - 1, 0, numMoves, numCheckmates);
#endif

    end = clock();
    float duration = (float) (end - begin) / CLOCKS_PER_SEC;

    bool pass = true;
    bool error;
    for (uint64_t i = 0; i < depth; ++i)
    {
        error = false;
        if (compare)
            error = numMoves[i] != compare[i];
        printf("Number of valid moves after %" PRIu64 " ply: %" PRIu64 " (%" PRIu64 " checkmates found)", i, numMoves[i], numCheckmates[i]);
        if (error)
        {
            printf(" ** ERROR **");
            pass = false;
        }
        printf("\n");
    }

    printf("Finished perft; elapsed time: %f seconds\n", duration);

#if !defined(MAKE_UNMAKE_MOVE) && !defined(NO_BOARD_STACK)
    BoardStackDestroy(&boardStack);
#endif

    return pass;
}

int main(int argc, char ** argv)
{
    int result = Init(NULL);
    if (result != 0)
        return result;

    typedef struct
    {
        const char * fen;
        uint64_t depth;
        uint64_t comparisons[16];
    } FENTest;

    static const FENTest a = { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0", 7, { 20, 400, 8902, 197281, 4865609, 119060324, 3195901860, 84998978956, 2439530234167, 69352859712417, 2097651003696806, 62854969236701747, 1981066775000396239 } };
    static const FENTest b = { "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 6, { 48, 2039, 97862, 4085603, 193690690, 8031647685 } };
    static const FENTest c = { "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", 8, { 14, 191, 2812, 43238, 674624, 11030083, 178633661, 3009794393 } };
    static const FENTest d = { "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6, { 6, 264, 9467, 422333, 15833292, 706045033 } };
    static const FENTest e = { "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 6, { 6, 264, 9467, 422333, 15833292, 706045033 } };
    static const FENTest f = { "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 5, { 44, 1486, 62379, 2103487, 89941194 } };
    static const FENTest g = { "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 6, { 46, 2079, 89890, 3894594, 164075551, 6923051137, 287188994746, 11923589843526, 490154852788714 } };
    static const FENTest h = { "rnb1kbnr/pp1pp1pp/1qp2p2/8/Q1P5/N7/PP1PPPPP/1RB1KBNR b Kkq - 2 4", 7, { 28, 741, 21395, 583456, 17251342, 490103130, 14794751816 } };

#if 1
    static const FENTest * tests[] = { &a, &b, &c, &d, &e, &f, &g, &h };
    //static const FENTest * tests[] = { &a };

    int len = sizeof(tests) / sizeof(tests[0]);

    bool pass = true;

    for (int i = 0; i < len; ++i)
    {
        if (!perft(tests[i]->fen, tests[i]->depth, tests[i]->comparisons))
            pass = false;
    }

    printf("%s\n", pass ? "PASS" : "FAIL");
#else
    static const char * FEN = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -";
    //static const char * FEN = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/1R2K2R b Kkq -";
    static const uint64_t Depth = 5;

    perft(FEN, Depth, 0);
#endif

    Cleanup();

    return 0;
}
