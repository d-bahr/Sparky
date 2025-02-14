#include "Evaluation.h"
#include "FEN.h"
#include "Intrinsics.h"
#include "KillerMove.h"
#include "Logger.h"
#include "MoveGeneration.h"
#include "MoveOrderer.h"
#include "OpeningBook.h"
#include "Options.h"
#include "Repetition.h"
#include "StaticEval.h"
#include "Syzygy.h"
#include "Transposition.h"
#include "Zobrist.h"

#include "tables/MoveTables.h"

#ifdef __GNUC__
#include <alloca.h>
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>

#ifdef _MSC_VER
#include "WindowsInclude.h"
static LONGLONG s_ticks = 0;

// Code for profiling a section:

/*LARGE_INTEGER start, end;
QueryPerformanceCounter(&start);

QueryPerformanceCounter(&end);
s_ticks += (end.QuadPart - start.QuadPart);*/

#endif

// TODO: Newer GCC versions support this, but disabled for now.
#ifdef __GNUC__
#define thread_local
#else
#include <threads.h>
#endif

#include "MinMax.h"

#define ENABLE_TT 0

#define ENABLE_SYZYGY 1

#define TIME_LIMIT (3 * CLOCKS_PER_SEC)

// Null move forward pruning depth reduction "R"
#define NULL_MOVE_PRUNING_DEPTH_REDUCTION 3

// Maximum length of search extensions, to limit things like long check sequences.
#define SEARCH_EXTENSION_MAX 3

static RepetitionTable s_repetitionTable;
static TranspositionTable s_transpositionTable;
static uint64_t s_lmrPruning = 0;
static uint64_t s_nullMovePruning = 0;
static uint64_t s_repetitions = 0;
static uint64_t s_extensions = 0;
static uint64_t s_positionsEvaluated = 0;
static uint64_t s_betaCutoffs = 0;
static uint64_t s_alphaUpdates = 0;
static uint64_t s_ttHits = 0;
static uint64_t s_tbHits = 0;
static uint64_t s_avgMoveOrderer = 0;
static uint64_t s_avgMoveOrdererCnt = 0;
static int s_selDepth = 0;

// Give or take enough space for maximum number of moves for MAX_LINE_DEPTH * 2 consecutive ply, which should be enough for any reasonably conceivable position.
static thread_local Move s_moves[MAX_LINE_DEPTH * 256 * 2];
static thread_local int32_t s_approxMoveScores[MAX_LINE_DEPTH * 256 * 2];
// TODO: Need to limit q-search to a certain ply to prevent overflow in s_moveLines.
static thread_local MoveLine s_moveLines[MAX_LINE_DEPTH * 2]; // Need some extra room for extra ply in quiescence search
// TODO: This might not need to be thread_local.
static thread_local KillerMoves s_killerMoves[MAX_LINE_DEPTH];

static volatile bool s_evalCanceled = false;

static inline void PrintMoveLine(const MoveLine * line)
{
    char moveStr[6]; // Max 5 chars plus extra character for null-termination
    for (int i = 0; i < line->length; ++i)
    {
        memset(moveStr, 0, sizeof(moveStr));
        if (0 != MoveToString(line->moves[i], moveStr, sizeof(moveStr) - 1))
        {
            if (i > 0)
                putc(' ', stdout);
            printf(moveStr); // No puts here... puts adds a newline.
        }
    }
    fflush(stdout);
}

static inline void PrintMove(Move move)
{
    char moveStr[6]; // Max 5 chars plus extra character for null-termination
    memset(moveStr, 0, sizeof(moveStr));
    if (0 != MoveToString(move, moveStr, sizeof(moveStr) - 1))
        puts(moveStr);
}

// The value stored in the transposition table is EVAL_CHECKMATE at the point of checkmate,
// decreasing by 1 for each ply to checkmate.
static inline int32_t ValueToTranspositionTable(int32_t eval, int32_t ply)
{
    // TODO: The CHECKMATE_WIN/LOSE checks might be unnecessary.
    if (eval >= EVAL_CHECKMATE)
        return eval + ply;
    else if (eval <= -EVAL_CHECKMATE)
        return eval - ply;
    else
        return eval;
}

// Subtract off the current ply from the checkmate eval, because it takes n additional ply
// to get to the mating sequence stored in the transposition table.
static inline int32_t ValueFromTranspositionTable(int32_t eval, int32_t ply)
{
    if (eval >= EVAL_CHECKMATE)
        return eval - ply;
    else if (eval <= -EVAL_CHECKMATE)
        return eval + ply;
    else
        return eval;
}

static inline Move UnpackCachedMove(const Board * board, EncodedMove cachedMove)
{
    Move m = MoveDecode(cachedMove);
    EncodedSquare from = SquareEncode(m.from);
    const uint64_t * pieceTables = (board->playerToMove == White) ? board->whitePieceTables : board->blackPieceTables;
    if (pieceTables[PIECE_TABLE_PAWNS] & from)
        m.piece = Pawn;
    else if (pieceTables[PIECE_TABLE_KNIGHTS] & from)
        m.piece = Knight;
    else if (pieceTables[PIECE_TABLE_BISHOPS_QUEENS] & from)
    {
        if (pieceTables[PIECE_TABLE_ROOKS_QUEENS] & from)
            m.piece = Queen;
        else
            m.piece = Bishop;
    }
    else if (pieceTables[PIECE_TABLE_ROOKS_QUEENS] & from)
        m.piece = Rook;
    else
        m.piece = King;
    return m;
}

// Quiescence or "quiet" search. Basically, continue searching through capture chains until we reach
// a "quiet" position where there are no winning tactical moves; i.e. captures.
static int32_t QuiescenceSearch(const Board * board, int32_t depth, int32_t alpha, int32_t beta, int32_t linePly, const MoveLine * bestLinePrev, MoveLine * bestLine, int moveCounter, bool pv)
{
    if (s_evalCanceled && linePly > 0)
        return 0;

    // TODO: Repetition check is not needed in quiescence search since we only check for captures and not checks,
    // but if we also test for checks, then it may be useful/necessary to include repetition checks here too.

    EncodedMove ttMove = 0;
#if ENABLE_TT
    // TODO: Probably don't need to check TT when depth == 0 because the same check should have happened in regular search.
    int32_t ttEval = 0;
    int32_t ttDepth = 0;
    TranspositionType ttTypeCached = TranspositionTableLookup(&s_transpositionTable, board->hash, &ttMove, &ttEval, &ttDepth);
    if (ttTypeCached != TranspositionNone && ttDepth >= (depth - (ttTypeCached == TranspositionExact)))
    {
        if (((ttTypeCached & TranspositionBeta) && ttEval >= beta) || ((ttTypeCached & TranspositionAlpha) && ttEval <= alpha))
        {
            s_ttHits++;

            // Only include move if the depth is zero (first q-search). Otherwise, we might add in a wrong move at the very end of the line,
            // with other q-search moves (absent in the saved line) in between this move and moves from the regular search.
            if (depth == 0 && EncodedMoveValid(ttMove))
            {
                // Note: We lose the line information with the transposition table, at least in the current search at the current depth.
                // Nothing we can really do to work around this.
                //bestLine->moves[0] = UnpackCachedMove(board, ttMove);
                //bestLine->length = 1;
            }
            return ValueFromTranspositionTable(ttEval, linePly);
        }
    }
#endif

    int32_t staticEval = Evaluate(board); // (board->playerToMove == White) ? board->staticEval : -board->staticEval; // Evaluate(board);
    //assert(staticEval == board->staticEval); // TODO: Eventually transition this over.

    if (board->playerToMove == Black)
        staticEval = -staticEval;

    if (staticEval >= beta)
    {
#if ENABLE_TT
        if (linePly > 0)
            TranspositionTableInsert(&s_transpositionTable, board->hash, (EncodedMove)0, ValueToTranspositionTable(staticEval, linePly), depth, TranspositionBeta);
#endif
        return staticEval; // TODO: Maybe return staticEval?
    }
    else if (staticEval > alpha)
        alpha = staticEval;

    Move * moves = &s_moves[moveCounter];
    MoveLine * line = &s_moveLines[linePly];
    MoveLineInit(line);
    Board nextBoard;
#if EVAL_PSEUDO_LEGAL
    bool hasValidMove = false;
    uint8_t numMoves = GetPseudoLegalCaptures(board, moves);
#else
    uint8_t numMoves = GetValidCaptures(board, moves);

    // At the end of quiescence search, do a full search one last time to see if checkmate happened.
    if (numMoves == 0)
    {
        if (IsCheckmate(board, playerToMove))
            return CHECKMATE_LOSE + linePly;
        // No valid moves and not in check; must be a quiet position.
        return alpha;
    }
#endif
    MoveOrderer moveOrderer;

    // Order moves via heuristic to improve performance of alpha-beta pruning.
    MoveOrdererInitialize(&moveOrderer, board, moves, &s_approxMoveScores[moveCounter], numMoves, linePly, bestLinePrev, &s_killerMoves[linePly], MoveDecode(ttMove));

    // Initialize null move. If no move is found within the alpha-beta cutoff, then this null move is
    // inserted into the transposition table; we haven't identified what the best move is due to cutoff,
    // but we do know what the upper limit is.
    Move bestMove = { 0 };
    bestMove.from = SquareA1;
    bestMove.to = SquareA1;
    bestMove.piece = 0;
    bestMove.promotion = 0;

    unsigned long long pieceCount = intrinsic_popcnt64(board->allPieceTables);
    if (pieceCount > 23)
        pieceCount = 23;
    float midGameProgressionScalar = MidGameScalar[pieceCount];
    float endGameProgressionScalar = EndGameScalar[pieceCount];
    PieceType capturedPiece;

    Move move;
    uint64_t cntt = 0;
    while (MoveOrdererGetNextMove(&moveOrderer, &move))
    {
        cntt++;
#if EVAL_PSEUDO_LEGAL
        if (!IsMoveValid(board, move))
            continue;

        hasValidMove = true;
#endif

#if 1
        // Delta pruning: if the capture can't raise the score by enough, then don't bother continuining further.
        // Also called q-search futility pruning.
        capturedPiece = BoardGetPieceAtSquare(board, move.to);
        if (capturedPiece == None)
        {
            // Special case; this must be en-passant.
            if (GetPieceValue(midGameProgressionScalar, endGameProgressionScalar, !board->playerToMove, Pawn, board->playerToMove == White ? SquareMoveRankDown(move.to) : SquareMoveRankUp(move.to)) + 2000 <= alpha)
                continue;
        }
        else
        {
            if (GetPieceValue(midGameProgressionScalar, endGameProgressionScalar, !board->playerToMove, Pawn, move.to) + 2000 <= alpha)
                continue;
        }
#endif

        nextBoard = *board;
        s_positionsEvaluated++;
        MakeMove(&nextBoard, move);

        // TODO: (12/30/2024) Validate this actually does what is expected. Intention is to prune additional moves.
        //bool givesCheck = KingIsAttacked(&nextBoard, nextBoard.playerToMove);
        //if (move.promotion != None && !givesCheck && alpha > -EVAL_CHECKMATE && moveCounter > 2)
        //    continue;

        int32_t score = -QuiescenceSearch(&nextBoard, depth - 1, -beta, -alpha, linePly + 1, bestLinePrev, line, moveCounter + numMoves, pv);
        if (score >= beta)
        {
#if ENABLE_TT
            if (linePly > 0)
                TranspositionTableInsert(&s_transpositionTable, board->hash, MoveEncode(move), ValueToTranspositionTable(score, linePly), depth, TranspositionBeta);
#endif
            bestLine->moves[0] = move;
            memcpy(bestLine->moves + 1, line->moves, line->length * sizeof(Move));
            bestLine->length = line->length + 1;
            return score; // TODO: Maybe return score?
        }
        if (score > alpha)
        {
            alpha = score;
            bestMove = move;
            bestLine->moves[0] = bestMove;
            memcpy(bestLine->moves + 1, line->moves, line->length * sizeof(Move));
            bestLine->length = line->length + 1;
        }
    }
    //s_avgMoveOrderer += cntt;
    //s_avgMoveOrdererCnt++;

#if EVAL_PSEUDO_LEGAL
    if (!hasValidMove)
    {
        // TODO: Try to devise a better way to handle this. This is rather expensive at the end of q-search.
        if (IsCheckmate(board))
            return CHECKMATE_LOSE + linePly;

        // No valid moves and not in check; must be a quiet position.
        return alpha;
    }
#endif

#if ENABLE_TT
    // No point in saving the root node to the transposition table. Also would give extremely abbreviated results on subsequent searches.
    if (linePly > 0)
        TranspositionTableInsert(&s_transpositionTable, board->hash, MoveEncode(bestMove), ValueToTranspositionTable(alpha, linePly), depth, TranspositionAlpha);
#endif
    return alpha;
}

static int32_t Minimax(const Board * board, int32_t alpha, int32_t beta, int32_t depth, int32_t linePly, const MoveLine * bestLinePrev, MoveLine * bestLine, int32_t totalExtension, int moveCounter, bool pv)
{
    if (s_evalCanceled && linePly > 0)
        return 0;

    if (board->halfmoveCounter >= 100)
    {
        // Draw by 50 move rule.
        return 0;
    }

    int numPiecesRemaining = (int)intrinsic_popcnt64(board->allPieceTables);
    if (numPiecesRemaining <= 2)
    {
        // Only pieces remaining are Kings; this is a draw.
        return 0;
    }
    else if (numPiecesRemaining == 3 &&
             (intrinsic_popcnt64(board->whitePieceTables[PIECE_TABLE_KNIGHTS]) == 1 ||
              intrinsic_popcnt64(board->blackPieceTables[PIECE_TABLE_KNIGHTS]) == 1 ||
              intrinsic_andn64(intrinsic_popcnt64(board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS]), intrinsic_popcnt64(board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS])) == 1 ||
              intrinsic_andn64(intrinsic_popcnt64(board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS]), intrinsic_popcnt64(board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS])) == 1))
    {
        // Only pieces remaining are two Kings and one bishop or knight (for one player). This is a draw due to lack of sufficient checkmating material.
        return 0;
    }

    if (RepetitionTableContains(&s_repetitionTable, board->hash))
    {
        // Draw by repetition. Or at least, we repeated a position once already, which means that no improvement was made
        // to the position, which implies an eventual draw.
        s_repetitions++;
        // Contempt: encourage playing for a win when in the early and mid games. As the game progresses, the chance of a draw increases.
        // In a king and pawn endgame, always use a true draw value to prevent blundering.
        if (numPiecesRemaining <= 2 + intrinsic_popcnt64(board->whitePieceTables[PIECE_TABLE_PAWNS]) + intrinsic_popcnt64(board->blackPieceTables[PIECE_TABLE_PAWNS]))
            return 0;

        int contempt = numPiecesRemaining * 10;
        return -contempt;
    }

    if (linePly > s_selDepth)
        s_selDepth = linePly;

    EncodedMove ttMove = 0;
    int32_t ttDepth = 0;
    TranspositionType ttTypeCached = TranspositionNone;

    // For root node, substitute the actual best move from the previous iteration for the transposition table lookup.
    if (pv && linePly == 0 && bestLinePrev != NULL && bestLinePrev->length > 0)
    {
        ttMove = MoveEncode(bestLinePrev->moves[0]);
        ttTypeCached = TranspositionExact;
    }
    else
    {
        // TODO: TT is wrong; something is screwed up. When this is enabled, start getting all sorts of bad moves.
#if 1
#if ENABLE_TT
        int32_t ttEval = 0;
        ttTypeCached = TranspositionTableLookup(&s_transpositionTable, board->hash, &ttMove, &ttEval, &ttDepth);

        if (linePly > 0)
        {
            // Prune if a mate was already found in fewer moves.
            if (alpha < CHECKMATE_LOSE + linePly)
                alpha = CHECKMATE_LOSE + linePly;
            if (beta > CHECKMATE_WIN - linePly)
                beta = CHECKMATE_WIN - linePly;
            if (alpha >= beta)
                return alpha;
        }

        if (!pv && (ttTypeCached != TranspositionNone) && (ttDepth > (depth - (ttTypeCached == TranspositionExact))))
        {
            if (ttTypeCached & ((ttEval >= beta) ? TranspositionBeta : TranspositionAlpha))
            {
                s_ttHits++;

                if (EncodedMoveValid(ttMove))
                {
                    // Note: We lose the line information with the transposition table, at least in the current search at the current depth.
                    // Nothing we can really do to work around this.
                    //bestLine->moves[0] = UnpackCachedMove(board, ttMove);
                    //bestLine->length = 1;
                }
                return ValueFromTranspositionTable(ttEval, linePly);
            }
        }
#endif
#endif
    }


#if ENABLE_SYZYGY
    if (linePly > 0 && MaxCardinality > 0)
    {
        if (intrinsic_popcnt64(board->allPieceTables) <= MaxCardinality && board->castleBits == 0 && board->halfmoveCounter == 0)
        {
            ProbeState probeResult;
            WDLScore wdlScore = SyzygyProbeWDL(board, &probeResult);
            if (probeResult != PROBE_STATE_FAIL)
            {
                s_tbHits++;

                int value;
                if (wdlScore < -1)
                {
                    value = -EVAL_CHECKMATE + linePly + 1;
                    if (value <= alpha)
                    {
                        TranspositionTableInsert(&s_transpositionTable, board->hash, (EncodedMove) 0, ValueToTranspositionTable(value, linePly), depth, TranspositionAlpha);
                        return value;
                    }
                }
                else if (wdlScore > 1)
                {
                    value = EVAL_CHECKMATE - linePly - 1;
                    if (value >= beta)
                    {
                        TranspositionTableInsert(&s_transpositionTable, board->hash, (EncodedMove) 0, ValueToTranspositionTable(value, linePly), depth, TranspositionBeta);
                        return value;
                    }
                }
                else
                {
                    value = 2 * wdlScore;
                    TranspositionTableInsert(&s_transpositionTable, board->hash, (EncodedMove) 0, ValueToTranspositionTable(value, linePly), depth, TranspositionExact);
                    return value;
                }
            }
        }
    }
#endif

    if (depth <= 0)
    {
#if 1
        return QuiescenceSearch(board, depth, alpha, beta, linePly, bestLinePrev, bestLine, moveCounter, pv);
#else
        int32_t staticEval = Evaluate(board);
        if (board->playerToMove == Black)
            staticEval = -staticEval;
        return staticEval;
#endif
    }

    Move * moves = &s_moves[moveCounter];
    MoveLine * line = &s_moveLines[linePly];
    MoveLineInit(line);
    Board nextBoard;

    bool inCheck = KingIsAttacked(board, board->playerToMove);

    int32_t staticEval = Evaluate(board); // (board->playerToMove == White) ? board->staticEval : -board->staticEval; // Evaluate(board);
    //assert(staticEval == board->staticEval); // TODO: Eventually transition this over.
    //int staticEval = (board->playerToMove == White) ? board->staticEval : -board->staticEval; // Evaluate(board);

    if (board->playerToMove == Black)
        staticEval = -staticEval;

    if (!pv && !inCheck)
    {
        // TODO: Futility pruning is seriously broken when it comes to finding mates.
#if 1
        // Futility pruning: skip moves near the end of the depth which cannot score well enough to make a difference. Checkmates are exempt.
        /*if (depth < 9 &&
            staticEval - (1400 * (depth - 1)) - 10000 >= beta &&
            staticEval < EVAL_CHECKMATE)
            return staticEval;*/
        if (depth < 9 && staticEval + 900 * depth + 1250 <= alpha && staticEval < EVAL_CHECKMATE && staticEval > -EVAL_CHECKMATE)
            return staticEval;
        if (depth < 9 && staticEval - (depth * 1400) >= beta && staticEval < EVAL_CHECKMATE && staticEval > -EVAL_CHECKMATE)
            return staticEval;
#endif

#if 1
        // Do null move pruning only if not in check (can't create an illegal situation -- this would totally screw up move generation).
        /*int nullMoveReduction = NULL_MOVE_PRUNING_DEPTH_REDUCTION;
        if (numPiecesRemaining <= 12)
            nullMoveReduction--;*/
        int32_t R = depth / 3 + 4;
        if (staticEval >= beta)
        {
            // Identical to Stockfish. Added staticEval >= beta test to ensure R isn't negative.
            int z = (staticEval - beta) / 1730;
            R += min(z, 6);
        }
        if (numPiecesRemaining >= 4 && depth >= R)
        {
            // Attempt to make a null move (null move forward pruning).
            nextBoard = *board;
            MakeNullMove(&nextBoard);

            int32_t score = -Minimax(&nextBoard, -beta, -beta + 1, depth - R, linePly + 1, NULL, line, totalExtension, moveCounter, false);
            if (score >= beta)
            {
                s_nullMovePruning++;
                return beta;
            }
        }
#endif
    }

    if (pv && !EncodedMoveValid(ttMove))
        depth -= min(depth, 2 + 2 * (ttTypeCached != TranspositionNone && ttDepth >= depth));
    if (!pv && depth >= 8 && !EncodedMoveValid(ttMove))
        depth -= 2;

#if EVAL_PSEUDO_LEGAL
    bool hasValidMove = false;
    uint8_t numMoves = GetPseudoLegalMoves(board, moves);
#else
    uint8_t numMoves = GetValidMoves(board, moves);
    if (numMoves == 0) // Checkmate?
    {
        // If king is in check, then game over: checkmate. Otherwise, stalemate.
        if (inCheck)
        {
            return CHECKMATE_LOSE + linePly;
        }
        else
        {
            return 0;
        }
    }
#endif
    MoveOrderer moveOrderer;

    // Order moves via heuristic to improve performance of alpha-beta pruning.
    // TODO: Should probably explicitly discard bestLinePrev when we are no longer looking at the PV.
    MoveOrdererInitialize(&moveOrderer, board, moves, &s_approxMoveScores[moveCounter], numMoves, linePly, bestLinePrev, &s_killerMoves[linePly], MoveDecode(ttMove));

    //if (pv && linePly == 0 && depth <= 1)
    //    MoveOrdererPrint(&moveOrderer);
    TranspositionType ttType = TranspositionAlpha;

    // Initialize null move. If no move is found within the alpha-beta cutoff, then this null move is
    // inserted into the transposition table; we haven't identified what the best move is due to cutoff,
    // but we do know what the upper limit is.
    Move bestMove = { 0 };
    /*bestMove.from = SquareA1;
    bestMove.to = SquareA1;
    bestMove.piece = 0;
    bestMove.promotion = 0;*/

    RepetitionTablePush(&s_repetitionTable, board->hash);

    Move move;
    int i = 0;
    uint64_t cntt = 0;
    // TODO: (12/30/2024) See if it is possible to implement some kind of hybrid-pseudo-legal move system to eliminate some of the "obvious"
    // impossible moves that would maybe make move ordering faster.
    while (MoveOrdererGetNextMove(&moveOrderer, &move))
    {
        cntt++;
        if (s_evalCanceled && linePly > 0)
        {
            RepetitionTablePop(&s_repetitionTable, board->hash);
            return 0;
        }

#if EVAL_PSEUDO_LEGAL
        if (!IsMoveValid(board, move))
            continue;

        hasValidMove = true;
#endif

        nextBoard = *board;
        s_positionsEvaluated++;
        MakeMove(&nextBoard, move);

        int32_t score = 0;
        bool fullSearch = true;

        bool isCapture = numPiecesRemaining > intrinsic_popcnt64(nextBoard.allPieceTables);

        bool isPromotion = move.promotion != None;

        bool givesCheck = KingIsAttacked(&nextBoard, nextBoard.playerToMove);

        bool lmrPass = false;

#if 0
        if (!pv && i > 0 && !givesCheck && !isPromotion && depth < 6 && staticEval + 900 * depth + 1250 <= alpha)
            continue;
#endif

        // Search extensions.
        // Extend search for:
        // - Checks
        // - Single response (one valid move)
        int32_t extension = 0;
#if 1
        if (totalExtension < SEARCH_EXTENSION_MAX)
        {
            if (numMoves == 1 || givesCheck || inCheck)
                extension++;

            if (isPromotion)
                extension++;

            // Limit the number of extensions per ply.
            if (extension >= 2)
                extension = 2;

            if (extension > 0)
                s_extensions++;
        }
#endif
        /*if (linePly > 0 && depth < 12 && !inCheck && !isCapture && !isPromotion && staticEval + 900 * depth + 1250 <= alpha && staticEval < EVAL_CHECKMATE && staticEval > -EVAL_CHECKMATE)
            continue;*/

#if 1
#define LMR_REDUCTION 1
        // Late move reductions (LMR)
        // Skip LMR for: promotions, captures, checks, and PV
        if (!pv &&
            i >= 2 &&
            depth >= (2 + LMR_REDUCTION) &&
            !isPromotion &&
            !isCapture &&
            !givesCheck &&
            !inCheck)
        {
            // TODO: Make this reduction variable. Needs a lot of testing to see.
            MoveLineInit(line);
            score = -Minimax(&nextBoard, -(alpha + 1), -alpha, depth - 1 - LMR_REDUCTION, linePly + 1, bestLinePrev, line, totalExtension, moveCounter + numMoves, false);
            if (score <= alpha)
            {
                // Not worth checking; prune.
                s_lmrPruning++;
                fullSearch = false;
            }
            else
            {
                lmrPass = true;
            }
        }
#endif

        /*static int32_t s_foo[64];
        static Move s_bar[64];
        static int32_t s_a[64];
        static int32_t s_b[64];
        s_foo[linePly] = INT_MIN;
        s_bar[linePly] = move;
        s_a[linePly] = alpha;
        s_b[linePly] = beta;*/
        MoveLineInit(line);
        if (fullSearch)
        {
            // If not in the principal variation, do a reduced search first.
            if ((!pv || i > 0) && !lmrPass)
                score = -Minimax(&nextBoard, -(alpha + 1), -alpha, depth - 1 + extension, linePly + 1, bestLinePrev, line, totalExtension + extension, moveCounter + numMoves, false);
            // Do a full search for principal variation or if the reduced search showed something promising.
            if (pv || i == 0 || (score > alpha && beta - alpha > 1))
                score = -Minimax(&nextBoard, -beta, -alpha, depth - 1 + extension, linePly + 1, bestLinePrev, line, totalExtension + extension, moveCounter + numMoves, pv);
        }
        /*s_foo[linePly] = score;
        printf("Line:");
        char moveStr[6];
        memset(moveStr, 0, sizeof(moveStr));
        for (int q = 0; q <= linePly; ++q)
        {
            if (0 != MoveToString(s_bar[q], moveStr, sizeof(moveStr) - 1))
            {
                if (s_foo[q] == INT_MIN)
                    printf(" ? (%i/%i %s) ", s_a[q], s_b[q], moveStr);
                else
                    printf(" %i (%i/%i %s) ", s_foo[q], s_a[q], s_b[q], moveStr);
            }
        }
        printf("\n");*/
        if (score >= beta)
        {
#if ENABLE_TT
            if (linePly > 0)
                TranspositionTableInsert(&s_transpositionTable, board->hash, MoveEncode(move), ValueToTranspositionTable(score, linePly), depth, TranspositionBeta);
#endif
            RepetitionTablePop(&s_repetitionTable, board->hash);
            // Store as a "killer" move so we can do smarter move ordering.
            if (!isCapture)
                KillerMoveAdd(&s_killerMoves[linePly], move);
            s_betaCutoffs++;
            bestLine->moves[0] = move;
            memcpy(bestLine->moves + 1, line->moves, line->length * sizeof(Move));
            bestLine->length = line->length + 1;
            return score;
        }
        if (score > alpha)
        {
            s_alphaUpdates++;
            alpha = score;
            if (pv)
                ttType = TranspositionExact;
            bestMove = move;
            bestLine->moves[0] = bestMove;
            memcpy(bestLine->moves + 1, line->moves, line->length * sizeof(Move));
            bestLine->length = line->length + 1;
        }

        i++;
    }
    s_avgMoveOrderer += cntt;
    s_avgMoveOrdererCnt++;

#if EVAL_PSEUDO_LEGAL
    if (!hasValidMove)
    {
        RepetitionTablePop(&s_repetitionTable, board->hash);

        // If king is in check, then game over: checkmate. Otherwise, stalemate.
        if (inCheck)
        {
            return CHECKMATE_LOSE + linePly;
        }
        else
        {
            return 0;
        }
    }
#endif

#if ENABLE_TT
    // No point in saving the root node to the transposition table. Also would give extremely abbreviated results on subsequent searches.
    // TODO: I suspect TT entries with mate in the eval are incorrect. Need to fix.
    if (linePly > 0)
        TranspositionTableInsert(&s_transpositionTable, board->hash, MoveEncode(bestMove), ValueToTranspositionTable(alpha, linePly), depth, ttType);
#endif
    RepetitionTablePop(&s_repetitionTable, board->hash);

    return alpha;
}

static inline bool OnlyOneMove(const Board * board, MoveLine * line)
{
    uint8_t numMoves = GetValidMoves(board, s_moves);
    if (numMoves == 1)
    {
        line->length = 1;
        line->moves[0] = s_moves[0];
        return true;
    }
    else
    {
        return false;
    }
}

static inline bool OpeningBookLine(const Board * board, MoveLine * line)
{
    Move move;
    if (OpeningBookFind(board, &move))
    {
        line->length = 1;
        line->moves[0] = move;
        return true;
    }
    return false;
}

bool EvalStart(const Board * board, uint32_t maxTime, uint32_t maxDepth, MoveLine * bestLine)
{
#ifdef _MSC_VER
    s_ticks = 0;
#endif

    s_evalCanceled = false;

    if (OpeningBookLine(board, bestLine))
        return true;

    MoveLineInit(bestLine);

    if (OnlyOneMove(board, bestLine))
        return true;

    MoveLine line;
    MoveLineInit(&line);

    EvalClear();

    int32_t score = 0;

    s_lmrPruning = 0;
    s_nullMovePruning = 0;
    s_repetitions = 0;
    s_extensions = 0;
    s_positionsEvaluated = 0;
    s_betaCutoffs = 0;
    s_alphaUpdates = 0;
    s_ttHits = 0;
    s_selDepth = 0;

    memset(s_killerMoves, 0, sizeof(s_killerMoves));

    clock_t begin = clock();

    // Iterative deepening.
    int depth = 1;
    if (maxDepth > MAX_LINE_DEPTH - 1)
        maxDepth = MAX_LINE_DEPTH - 1;
    int32_t alphaAspirated = -EVAL_MAX;
    int32_t betaAspirated = EVAL_MAX;
// These values are determined empirically to give the lowest number of nodes visited.
// Will depend on the board position(s) tested against.
#define ALPHA_ASPIRATION_DEFAULT 600
#define BETA_ASPIRATION_DEFAULT 600
    int32_t alphaAspirationQty = ALPHA_ASPIRATION_DEFAULT;
    int32_t betaAspirationQty = BETA_ASPIRATION_DEFAULT;
    int aspirationFailures = 0;
    for (; depth <= (int)maxDepth; ++depth)
    {
        s_selDepth = 0;
        line.length = 0;
        score = Minimax(board, alphaAspirated, betaAspirated, depth, 0, bestLine, &line, 0, 0, true);

        // Aspiration windows. Use a tighter tolerance for alpha and beta on successive iterations to hopefully cull
        // nodes. If it fails high or low, we need to bite the bullet and increase the window size. Ideally, this
        // happens infrequently, but there will always be a tradeoff depending on how aggressive the windows are.
        if (score <= alphaAspirated)
        {
            // Try again, but with larger aspiration window.
            alphaAspirated = score - alphaAspirationQty;
            alphaAspirationQty *= 2;
            --depth; // Search again at same depth.
            aspirationFailures++;
            continue;
        }
        else if (score >= betaAspirated)
        {
            // Try again, but with larger aspiration window.
            betaAspirated = score + betaAspirationQty;
            betaAspirationQty *= 2;
            --depth; // Search again at same depth.
            aspirationFailures++;
            continue;
        }
        else
        {
            alphaAspirationQty = ALPHA_ASPIRATION_DEFAULT;
            betaAspirationQty = BETA_ASPIRATION_DEFAULT;
            if (score <= -EVAL_CHECKMATE)
                alphaAspirated = CHECKMATE_LOSE;
            else
                alphaAspirated = score - alphaAspirationQty;
            if (score >= EVAL_CHECKMATE)
                betaAspirated = CHECKMATE_WIN;
            else
                betaAspirated = score + betaAspirationQty;
        }

        if (s_evalCanceled)
        {
            break;
        }
        else
        {
            clock_t now = clock();
            float duration = ((now == begin) ? 1.0f : (float) (now - begin)) / CLOCKS_PER_SEC;

            bool isMate = (score >= EVAL_CHECKMATE) || (score <= -EVAL_CHECKMATE);
            int32_t scoreOrMateIn = score / 10;
            if (isMate)
            {
                // Note: convert ply to moves, per UCI spec.
                if (score >= EVAL_CHECKMATE)
                    scoreOrMateIn = (CHECKMATE_WIN - score + 1) / 2;
                else
                    scoreOrMateIn = (CHECKMATE_LOSE - score - 1) / 2;
            }

            if (g_optionDebugMode)
            {
                printf("info depth %i seldepth %i score %s %i nodes %" PRIu64 " nps %" PRIu64 " multipv 1 hashfull %" PRIu64 " tbhits %" PRIu64 " time %" PRIu64 " pv ",
                       depth,
                       s_selDepth,
                       isMate ? "mate" : "cp",
                       scoreOrMateIn,
                       s_positionsEvaluated,
                       (uint64_t) (((double) s_positionsEvaluated) / duration),
                       (uint64_t) TranspositionTableGetUtilization(&s_transpositionTable),
                       s_tbHits,
                       (uint64_t) (duration * 1000));
                PrintMoveLine(&line);
                printf("\n");
                fflush(stdout);
            }

            LoggerLogLinef("info depth %i seldepth %i score %s %i nodes %" PRIu64 " nps %" PRIu64 " multipv 1 hashfull %" PRIu64 " tbhits %" PRIu64 " time %" PRIu64,
                           depth,
                           s_selDepth,
                           isMate ? "mate" : "cp",
                           scoreOrMateIn,
                           s_positionsEvaluated,
                           (uint64_t) (((double) s_positionsEvaluated) / duration),
                           (uint64_t) TranspositionTableGetUtilization(&s_transpositionTable),
                           s_tbHits,
                           (uint64_t) (duration * 1000));

            *bestLine = line;

            if ((uint32_t)(duration * 1000) >= maxTime)
                break;
        }

#if 0
        if (score >= EVAL_CHECKMATE)
        {
            printf("Mate in %i\n", (CHECKMATE_WHITE - score + 1) / 2);
            break;
        }
        else if (score <= -EVAL_CHECKMATE)
        {
            printf("Mate in %i\n", (score - CHECKMATE_BLACK + 1) / 2);
            break;
        }
#endif
    }

#if 0
#if 1
    printf("repetitions: %" PRIu64 "\n", s_repetitions);
#endif

#if 1
    printf("Avg moves: %f\n", (float) s_avgMoveOrderer / (float) s_avgMoveOrdererCnt);
#endif

#if 1
    printf("Aspiration failures: %i\n", aspirationFailures);
#endif

#if 1
    printf("tthits: %" PRIu64 "\n", s_ttHits);
#endif

#if 1
    printf("Beta cutoffs: %" PRIu64 "\n", s_betaCutoffs);
#endif

#if 1
    printf("Alpha updates: %" PRIu64 "\n", s_alphaUpdates);
#endif

#if 0
#ifdef _MSC_VER
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    printf("Profiling: %i ms\n", (int)(1000 * s_ticks / frequency.QuadPart));
#endif
#endif

#endif

    return bestLine->length > 0;
}

void EvalStop()
{
    s_evalCanceled = true;
}

bool EvalInit(size_t numTTBuckets)
{
    // 128MB transposition table cache (fixed size).
    if (!TranspositionTableInitialize(&s_transpositionTable, numTTBuckets))
        return false;

    // 16MB starting cache size for repetition table; can grow indefinitely.
    if (!RepetitionTableInitialize(&s_repetitionTable, 0x40000))
        return false;

    StaticEvalInitialize();

    return true;
}

void EvalClear()
{
    TranspositionTableClear(&s_transpositionTable);
    RepetitionTableClear(&s_repetitionTable);
}

void EvalDestroy()
{
    TranspositionTableDestroy(&s_transpositionTable);
    RepetitionTableDestroy(&s_repetitionTable);
}
