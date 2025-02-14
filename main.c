#include "Board.h"
#include "BoardStack.h"
#include "Evaluation.h"
#include "ConditionVariable.h"
#include "FEN.h"
#include "File.h"
#include "Init.h"
#include "Logger.h"
#include "Move.h"
#include "MoveGeneration.h"
#include "Mutex.h"
#include "Options.h"
#include "Piece.h"
#include "PieceType.h"
#include "Player.h"
#include "Rank.h"
#include "Square.h"
#include "StringStruct.h"
#include "Syzygy.h"
#include "Thread.h"
#include "ThreadPool.h"
#include "Transposition.h"
#include "Word.h"
#include "Zobrist.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SPARKY_VERSION "0.1"
#define DEFAULT_TT_SIZE_BUCKETS 0x200000
#define DEFAULT_TT_SIZE_MB TranspositionTableConvertSize(DEFAULT_TT_SIZE_BUCKETS)

bool ParseBoardSetup(Board * board, WordIterator * iter)
{
    if (!WordIteratorValid(iter))
        return false;

    const String * str = WordIteratorGet(iter);
    if (StringIEquals(str, "fen"))
    {
        size_t fullFenStrLen = 0;
        size_t fenWordCount = 0;
        char fullFenStr[256] = { 0 }; // Large enough for full FEN unless malformed.

        WordIteratorNext(iter);
        if (!WordIteratorValid(iter))
            return false;

        do
        {
            str = WordIteratorGet(iter);
            Move dummyMove;
            if (StringIEquals(str, "moves") || ParseMove(StringGetChars(str), &dummyMove)) // End of FEN.
                break;
            if (StringLength(str) + fullFenStrLen >= sizeof(fullFenStr) / sizeof(fullFenStr[0]) - 2) // Leave 1 char for null-terminator and 1 for (optional) space.
                return false;
            if (fenWordCount > 0)
            {
                fullFenStr[fullFenStrLen] = ' ';
                fullFenStrLen++;
            }
            strncpy(&fullFenStr[fullFenStrLen], StringGetChars(str), sizeof(fullFenStr) / sizeof(fullFenStr[0]) - fullFenStrLen);
            fenWordCount++;
            fullFenStrLen += StringLength(str);
            WordIteratorNext(iter);
            if (fenWordCount >= 6) // End of FEN.
                break;
        }
        while (WordIteratorValid(iter));

        assert(fullFenStrLen < sizeof(fullFenStr) / sizeof(fullFenStr[0]));
        fullFenStr[fullFenStrLen] = '\0';
        if (!ParseFEN(fullFenStr, board))
            return false;
    }
    else if (StringIEquals(str, "startpos"))
    {
        BoardInitializeStartingPosition(board);
        WordIteratorNext(iter);
    }

    if (!WordIteratorValid(iter))
        return true;

    // Treat "moves" word as optional.
    str = WordIteratorGet(iter);
    if (StringIEquals(str, "moves"))
    {
        WordIteratorNext(iter);
        if (!WordIteratorValid(iter))
            return true;
    }

    do
    {
        str = WordIteratorGet(iter);
        Move move;
        if (!ParseMove(StringGetChars(str), &move))
            return false;
        EncodedSquare encoded = SquareEncode(move.from);
        // Determine which piece is being moved.
        if (board->playerToMove == White)
        {
            if (board->whitePieceTables[PIECE_TABLE_PAWNS] & encoded)
                move.piece = Pawn;
            else if (board->whitePieceTables[PIECE_TABLE_KNIGHTS] & encoded)
                move.piece = Knight;
            else if (board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] & encoded)
            {
                if (board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] & encoded)
                    move.piece = Queen;
                else
                    move.piece = Bishop;
            }
            else if (board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] & encoded)
                move.piece = Rook;
            else if (move.from == board->whiteKingSquare)
                move.piece = King;
            else
                return false;
        }
        else
        {
            if (board->blackPieceTables[PIECE_TABLE_PAWNS] & encoded)
                move.piece = Pawn;
            else if (board->blackPieceTables[PIECE_TABLE_KNIGHTS] & encoded)
                move.piece = Knight;
            else if (board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] & encoded)
            {
                if (board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] & encoded)
                    move.piece = Queen;
                else
                    move.piece = Bishop;
            }
            else if (board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] & encoded)
                move.piece = Rook;
            else if (move.from == board->blackKingSquare)
                move.piece = King;
            else
                return false;
        }
        MakeMove(board, move);
        WordIteratorNext(iter);
    }
    while (WordIteratorValid(iter));

    return true;
}

static void SetOption(const String * name, const String * value)
{
    if (StringIEquals(name, "Hash"))
    {
        const char * c = StringGetChars(value);
        uint32_t sizeMB = 0;
        if (ParseIntegerFromString(&c, &sizeMB))
        {
            EvalDestroy();
            EvalInit(TranspositionTableConvertNumBuckets(sizeMB));
            LoggerLogLinef("Set new transposition table size: %" PRIu32 " MB", sizeMB);
        }
    }
    else if (StringIEquals(name, "Clear Hash"))
    {
        EvalClear();
        LoggerLogLine("Cleared transposition table");
    }
    else if (StringIEquals(name, "Debug Log File"))
    {
        const char * c = StringGetChars(value);
        LoggerSetFilename(c);
    }
    else if (StringIEquals(name, "Move Overhead"))
    {
        const char * c = StringGetChars(value);
        uint32_t overheadMs = 0;
        if (ParseIntegerFromString(&c, &overheadMs))
        {
            g_optionMoveOverhead = overheadMs;
            LoggerLogLinef("Set move overhead: %" PRIu32 " ms", overheadMs);
        }
    }
}

typedef struct
{
    Board * board;
    MoveLine line;
    uint32_t optimalMoveTime;
    uint32_t maxDepth;
    bool commit;

} EvalContext;

static ConditionVariable s_sleeper;
static Mutex s_sleeperMutex;

static void EvalThread(void * param)
{
    EvalContext * context = (EvalContext *) param;

    if (EvalStart(context->board, context->optimalMoveTime, context->maxDepth, &context->line))
    {
        if (context->commit)
            MakeMove(context->board, context->line.moves[0]);

        char moveStr[6]; // Max 5 chars plus extra character for null-termination
        memset(moveStr, 0, sizeof(moveStr));
        if (0 != MoveToString(context->line.moves[0], moveStr, sizeof(moveStr) - 1))
        {
            LoggerLogLinef("bestmove %s", moveStr);

            printf("bestmove %s", moveStr);
#if 0 // ponderhit and async pondering is not yet implemented
            if (context->line.length > 1)
            {
                memset(moveStr, 0, sizeof(moveStr));
                if (0 != MoveToString(context->line.moves[1], moveStr, sizeof(moveStr) - 1))
                    printf(" ponder %s", moveStr);
            }
#endif
            printf("\n");

            // Required for use with GUIs, to make sure buffered printf/puts is actually written to stdout.
            fflush(stdout);
        }
        else
        {
            // This would be programmer error somewhere
            assert(false);
        }
    }
    else
    {
        // TODO: Not really sure what the error handling should be in this case...
    }

    ConditionVariableSignalAll(&s_sleeper);
}

static void EvalTimedStop(void * param)
{
    size_t * millis = (size_t *) param;
    MutexLock(&s_sleeperMutex);
    ConditionVariableWaitTimeout(&s_sleeper, &s_sleeperMutex, *millis);
    MutexUnlock(&s_sleeperMutex);
    EvalStop();
}

static void CommandGo(Board * board, WordIterator * iter)
{
    EvalContext * context = (EvalContext *) malloc(sizeof(EvalContext));
    if (context != NULL)
    {
        context->board = board;
        context->maxDepth = MAX_LINE_DEPTH;
        context->optimalMoveTime = 0xFFFFFFFF;
        context->commit = false;

        uint32_t totalTime = 0xFFFFFFFF;
        uint32_t moveTime = 0xFFFFFFFF;
        uint32_t timeIncrement = 0xFFFFFFFF;
        uint32_t movesToGo = 0;

        const String * str;

        while (WordIteratorValid(iter))
        {
            str = WordIteratorGet(iter);
            WordIteratorNext(iter);

            if (StringIEquals(str, "infinite"))
            {
                context->maxDepth = MAX_LINE_DEPTH;
                context->optimalMoveTime = 0xFFFFFFFF;
                totalTime = 0xFFFFFFFF;
                moveTime = 0xFFFFFFFF;
            }
            else if (StringIEquals(str, "depth"))
            {
                if (WordIteratorValid(iter))
                {
                    str = WordIteratorGet(iter);
                    WordIteratorNext(iter);
                    const char * zz = StringGetChars(str);
                    uint32_t depth = 0;
                    if (ParseIntegerFromString(&zz, &depth))
                        context->maxDepth = depth;
                }
            }
            else if (StringIEquals(str, "movetime"))
            {
                if (WordIteratorValid(iter))
                {
                    str = WordIteratorGet(iter);
                    WordIteratorNext(iter);
                    const char * zz = StringGetChars(str);
                    uint32_t time = 0;
                    if (ParseIntegerFromString(&zz, &time))
                        moveTime = time;
                }
            }
            else if (StringIEquals(str, "wtime"))
            {
                if (WordIteratorValid(iter))
                {
                    str = WordIteratorGet(iter);
                    WordIteratorNext(iter);

                    if (board->playerToMove == White)
                    {
                        const char * zz = StringGetChars(str);
                        uint32_t time = 0;
                        if (ParseIntegerFromString(&zz, &time))
                            totalTime = time;
                    }
                }
            }
            else if (StringIEquals(str, "btime"))
            {
                if (WordIteratorValid(iter))
                {
                    str = WordIteratorGet(iter);
                    WordIteratorNext(iter);

                    if (board->playerToMove == Black)
                    {
                        const char * zz = StringGetChars(str);
                        uint32_t time = 0;
                        if (ParseIntegerFromString(&zz, &time))
                            totalTime = time;
                    }
                }
            }
            else if (StringIEquals(str, "winc"))
            {
                if (WordIteratorValid(iter))
                {
                    str = WordIteratorGet(iter);
                    WordIteratorNext(iter);

                    if (board->playerToMove == White)
                    {
                        const char * zz = StringGetChars(str);
                        uint32_t time = 0;
                        if (ParseIntegerFromString(&zz, &time))
                            timeIncrement = time;
                    }
                }
            }
            else if (StringIEquals(str, "binc"))
            {
                if (WordIteratorValid(iter))
                {
                    str = WordIteratorGet(iter);
                    WordIteratorNext(iter);

                    if (board->playerToMove == Black)
                    {
                        const char * zz = StringGetChars(str);
                        uint32_t time = 0;
                        if (ParseIntegerFromString(&zz, &time))
                            timeIncrement = time;
                    }
                }
            }
            else if (StringIEquals(str, "movestogo"))
            {
                if (WordIteratorValid(iter))
                {
                    str = WordIteratorGet(iter);
                    WordIteratorNext(iter);
                    const char * zz = StringGetChars(str);
                    uint32_t time = 0;
                    if (ParseIntegerFromString(&zz, &time))
                        movesToGo = time;
                }
            }
            // Custom parameter; applies the best move to the current board position after evaluation finishes.
            else if (StringIEquals(str, "commit"))
            {
                context->commit = true;
            }
        }

        // Calculate time to spend on this move.
        uint32_t maxTimeOnMove = 0xFFFFFFFF;
        uint32_t optimalTimeOnMove = 0xFFFFFFFF;

        if (moveTime != 0xFFFFFFFF)
        {
            maxTimeOnMove = moveTime - g_optionMoveOverhead;
            optimalTimeOnMove = moveTime;
        }
        else if (totalTime != 0xFFFFFFFF)
        {
            // Have enough time remaining for at most 40 moves. As time decreases, this will slowly cause moves to speed up.
            if (movesToGo == 0 || movesToGo > 40)
                movesToGo = 40;
            maxTimeOnMove = totalTime / movesToGo;
            // Include increment if present.
            if (timeIncrement != 0xFFFFFFFF)
                maxTimeOnMove += timeIncrement;
            // Subtract off any latency time between the engine and the GUI.
            maxTimeOnMove -= g_optionMoveOverhead;
            // Set optimal time usage to be 75% of the maximum time.
            // This math does the division first intentionally, both to avoid overflow and to be conservative (value will be rounded down slightly).
            optimalTimeOnMove = (maxTimeOnMove / 4) * 3;
        }

        context->optimalMoveTime = optimalTimeOnMove;

        if (ThreadPoolQueue(&EvalThread, context, &free))
        {
            if (maxTimeOnMove != 0xFFFFFFFF)
            {
                size_t * timeoutMs = (size_t *) malloc(sizeof(size_t));
                *timeoutMs = maxTimeOnMove;
                if (!ThreadPoolQueue(&EvalTimedStop, timeoutMs, free))
                    free(timeoutMs);
            }
        }
        else
        {
            free(context);
        }
    }
}

int main(int argc, char ** argv)
{
    if (!LoggerInit("log.txt"))
        return 1;

    int result = Init(NULL);
    if (result != 0)
        return result;

    if (!EvalInit(0x200000))
        return 1;

    ZobristGenerate();

    srand((unsigned int)time(NULL));

    if (!ThreadPoolInitialize(2))
        return 1;

    if (!ConditionVariableInitialize(&s_sleeper))
        return 1;

    if (!MutexInitialize(&s_sleeperMutex))
        return 1;

#ifdef _WIN32
    bool syzygyInitialized = SyzygyInit(".;syzygy\\3-4-5-dtz-nr;syzygy\\3-4-5-wdl;..\\..\\syzygy\\3-4-5-dtz-nr;..\\..\\syzygy\\3-4-5-wdl");
#else
    bool syzygyInitialized = SyzygyInit(".:syzygy/3-4-5-dtz-nr:syzygy/3-4-5-wdl:../../syzygy/3-4-5-dtz-nr:../../syzygy/3-4-5-wdl");
#endif

    if (!syzygyInitialized)
        puts("Warning: Syzygy end game tablebase not found.\n");

    char c = 0;
    Board board;
    BoardInitializeStartingPosition(&board);

    //if (!ParseFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 0", &board))
    //    return 1;
    //if (!ParseFEN("3rk2r/p1ppqpb1/1n3Bp1/3p4/4P1N1/8/PPP1QPpP/R3K1R1 b Qk - 0 0", &board))
    //    return 1;
    //if (!ParseFEN("3rk2r/p1ppqp2/1n3bp1/3pP3/6N1/8/PPP1QPpP/R3K1R1 b Qk - 0 0", &board))
    //    return 1;
    //if (!ParseFEN("3rr1k1/p1ppq3/1n4R1/3pbp2/7P/8/PPP1QP1N/2KR4 b - - 0 0", &board))
    //    return 1;
    //if (!ParseFEN("3rrk2/p1pp4/1n4R1/3p1Q2/7q/8/PKP2P1N/3R4 b - - 0 0", &board))
    //    return 1;
    //if (!ParseFEN("4R1k1/2p3b1/5pP1/5Q2/n2p4/1q6/2r2P2/5K2 b - - 1 40", &board))
    //    return 1;
    //if (!ParseFEN("6k1/2p5/8/3K4/8/8/8/5q2 b - - 8 81", &board))
    //    return 1;
    //if (!ParseFEN("8/6k1/2Kp4/1n3q2/8/8/8/8 b - - 10 97", &board))
    //    return 1;
    //if (!ParseFEN("rnbqkbnr/pppp1ppp/8/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b KQkq - 1 3", &board))
    //    return 1;
    String line;
    StringInitialize(&line);
    String nextWord;
    StringInitialize(&nextWord);
    WordList words;
    WordListInitialize(&words);
    while (0 != fread(&c, 1, 1, stdin))
    {
        if (c == '\n')
        {
            // Handle last word if needed.
            if (StringLength(&nextWord) > 0)
            {
                WordListPushString(&words, &nextWord);
                StringClear(&nextWord);
            }

            LoggerLog("Received command: ");
            LoggerLogLine(StringGetChars(&line));

            StringClear(&line);

            // Process input.
            if (WordListLength(&words) > 0)
            {
                WordIterator iter = WordListBegin(&words);
                if (WordIteratorValid(&iter))
                {
                    const String * str = WordIteratorGet(&iter);
                    WordIteratorNext(&iter);

                    if (StringIEquals(str, "position"))
                        ParseBoardSetup(&board, &iter);
                    else if (StringIEquals(str, "isready"))
                        puts("readyok");
                    else if (StringIEquals(str, "uci"))
                    {
                        puts("id name Sparky " SPARKY_VERSION);
                        puts("id author Sparky Development Team");
                        puts("option name Debug Log File type string default");
                        printf("option name Hash type spin default %" PRIu64 " min 4 max 1048576\n", (uint64_t)DEFAULT_TT_SIZE_MB);
                        puts("option name Clear Hash type button");
                        puts("option name Move Overhead type spin default 100 min 0 max 5000");
                        puts("uciok");
                    }
                    else if (StringIEquals(str, "debug"))
                    {
                        if (WordIteratorValid(&iter))
                        {
                            const String * str = WordIteratorGet(&iter);
                            if (StringIEquals(str, "on"))
                                g_optionDebugMode = true;
                            else if (StringIEquals(str, "off"))
                                g_optionDebugMode = false;
                        }
                    }
                    else if (StringIEquals(str, "go"))
                    {
                        CommandGo(&board, &iter);
                    }
                    // Custom parameter; applies a move to the board position without needing to specify the complete fen.
                    else if (StringIEquals(str, "nextmove"))
                    {
                        if (WordIteratorValid(&iter))
                        {
                            str = WordIteratorGet(&iter);
                            WordIteratorNext(&iter);

                            const char * zz = StringGetChars(str);
                            Move move;
                            if (ParseMove(zz, &move))
                            {
                                Player playerMoving = White;
                                if (MakeMove2(&board, move, &playerMoving))
                                    puts("ok");
                            }
                        }
                    }
                    // Custom parameter; prints the board state in FEN.
                    else if (StringIEquals(str, "printfen"))
                    {
                        char str[128];
                        memset(str, 0, sizeof(str));
                        ToFEN(&board, str, sizeof(str) - 1);
                        puts(str);
                    }
                    else if (StringIEquals(str, "stop"))
                    {
                        ConditionVariableSignalAll(&s_sleeper);
                        EvalStop();
                        ThreadPoolSync();
                    }
                    else if (StringIEquals(str, "quit"))
                    {
                        break;
                    }
                    else if (StringIEquals(str, "ucinewgame"))
                    {
                        EvalStop();
                        EvalClear();
                        ThreadPoolSync();
                    }
                    else if (StringIEquals(str, "setoption"))
                    {
                        if (WordIteratorValid(&iter))
                        {
                            str = WordIteratorGet(&iter);
                            WordIteratorNext(&iter);

                            if (StringIEquals(str, "name"))
                            {
                                String optionName;
                                String optionValue;
                                if (StringInitialize(&optionName))
                                {
                                    if (StringInitialize(&optionValue))
                                    {
                                        while (WordIteratorValid(&iter))
                                        {
                                            str = WordIteratorGet(&iter);
                                            WordIteratorNext(&iter);

                                            if (StringIEquals(str, "value"))
                                                break;

                                            if (StringLength(&optionName) > 0)
                                                StringPush(&optionName, ' ');
                                            StringConcat(&optionName, str);
                                        }

                                        while (WordIteratorValid(&iter))
                                        {
                                            str = WordIteratorGet(&iter);
                                            WordIteratorNext(&iter);

                                            if (StringLength(&optionValue) > 0)
                                                StringPush(&optionValue, ' ');
                                            StringConcat(&optionValue, str);
                                        }

                                        SetOption(&optionName, &optionValue);
                                        StringDestroy(&optionValue);
                                    }
                                    StringDestroy(&optionName);
                                }
                            }
                        }
                    }
                    // TODO:
                    // ponderhit (Used for move speculation while opponent is thinking/moving. Not yet implemented.)
                }
                WordListClear(&words);
            }

            // Required for use with GUIs, to make sure buffered printf/puts is actually written to stdout.
            fflush(stdout);
        }
        else
        {
            StringPush(&line, c);

            if (isspace(c))
            {
                if (StringLength(&nextWord) == 0)
                    continue; // Arbitrary whitespace is allowed; skip extra whitespace.

                WordListPushString(&words, &nextWord);
                StringClear(&nextWord);
            }
            else
            {
                StringPush(&nextWord, c);
            }
        }
    }
    LoggerLogLine("Exiting...");
    WordListDestroy(&words);
    StringDestroy(&nextWord);
    StringDestroy(&line);
    if (syzygyInitialized)
        SyzygyDestroy();
    EvalDestroy();
    ThreadPoolDestroy();
    Cleanup();
    LoggerDestroy();
    return 0;
}
