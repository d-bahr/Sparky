#include "Evaluation.h"
#include "FEN.h"
#include "MoveGeneration.h"
#include "MoveOrderer.h"
#include "StaticEval.h"

#include "tables/MoveTables.h"

#include <stdio.h>

#define MOVE_ORDERER_PRE_SORT 0

void MoveOrdererInitialize(MoveOrderer * moveOrderer, const Board * board, Move * moves, int32_t * approxScores, uint8_t numMoves, int32_t linePly, const MoveLine * bestLinePrev, const KillerMoves * killerMoves, Move ttMove)
{
    const uint64_t * opponentPieceTables;
    const uint64_t * friendlyPawnAttacks;
    if (board->playerToMove == White)
    {
        opponentPieceTables = board->blackPieceTables;
        friendlyPawnAttacks = s_pawnAttackBitboardWhite;
    }
    else
    {
        opponentPieceTables = board->whitePieceTables;
        friendlyPawnAttacks = s_pawnAttackBitboardBlack;
    }

    unsigned long long pieceCount = intrinsic_popcnt64(board->allPieceTables);
    const int32_t * pieceValueTable = (pieceCount > 10) ? PieceValuesMillipawnsMidGame : PieceValuesMillipawnsEndGame;

    // Order moves by a very simple approximate method of determining high vs. low value, to assist in alpha-beta pruning.
    // Goal is that high value moves are first, low value moves are last.
    for (uint8_t i = 0; i < numMoves; ++i)
    {
        Square square = moves[i].to;
        EncodedSquare encoded = SquareEncode(square);
        PieceType piece = moves[i].piece;

        // Start with score equal just to difference in static position score.
        //int32_t pieceToSquareValue = GetPieceValue(midGameProgressionScalar, endGameProgressionScalar, playerToMove, piece, square);
        //approxScores[i] = pieceToSquareValue - GetPieceValue(midGameProgressionScalar, endGameProgressionScalar, playerToMove, piece, moves[i].from);
        int32_t pieceToSquareValue = pieceValueTable[piece];
        approxScores[i] = 0;

        // If present the move from the transposition table should come first. This is the single greatest benefit to move ordering.
        if (MoveEquals(ttMove, moves[i]))
        {
            approxScores[i] += 100000000;
            continue; // No point to doing the rest, it won't matter (nothing else can lower the score enough to make a difference).
        }

        // If the move is part of the best line from the previous iteration (iterative deepening)
        // then give this move a score boost to make it come first.
        if (bestLinePrev != NULL && bestLinePrev->length > linePly && MoveEquals(bestLinePrev->moves[linePly], moves[i]))
        {
            approxScores[i] += 1000000;
            continue; // No point to doing the rest, it won't matter (nothing else can lower the score enough to make a difference).
        }

        // Capturing a piece is good.
        if (opponentPieceTables[PIECE_TABLE_COMBINED] & encoded)
        {
            if (opponentPieceTables[PIECE_TABLE_PAWNS] & encoded)
                approxScores[i] += pieceValueTable[Pawn];
            else if (opponentPieceTables[PIECE_TABLE_KNIGHTS] & encoded)
                approxScores[i] += pieceValueTable[Knight];
            else if (opponentPieceTables[PIECE_TABLE_BISHOPS_QUEENS] & encoded)
            {
                if (opponentPieceTables[PIECE_TABLE_ROOKS_QUEENS] & encoded)
                    approxScores[i] += pieceValueTable[Queen];
                else
                    approxScores[i] += pieceValueTable[Bishop];
            }
            else if (opponentPieceTables[PIECE_TABLE_ROOKS_QUEENS] & encoded)
                approxScores[i] += pieceValueTable[Rook];
            else
            {
                // Because we use pseudo-legal moves, this is technically possible (invalid move leads to capture of a king).
                // In this case, reduce the score by a lot, so it should get pruned out.
                assert(square == (board->playerToMove == White ? board->blackKingSquare : board->whiteKingSquare));
                approxScores[i] -= 100000000;
            }
        }
        else
        {
            if (KillerMoveFind(killerMoves, moves[i]))
            {
                // Give killer moves a little bump: we should be able to prune from these.
                // These are set to be slightly more valuable than a pawn.
                approxScores[i] += 1500;
            }
        }

        // Promotion is good.
        if (piece == Pawn)
        {
            if (moves[i].promotion != None)
                approxScores[i] += pieceValueTable[moves[i].promotion] - pieceToSquareValue;
        }

        // Penalize moving pieces to targettable locations.
        if ((GetPawnCaptureMoves(friendlyPawnAttacks, square) & opponentPieceTables[PIECE_TABLE_PAWNS]) ||
            (GetKnightMoves(square) & opponentPieceTables[PIECE_TABLE_KNIGHTS]) ||
            (GetBishopMoves(board->allPieceTables, square) & opponentPieceTables[PIECE_TABLE_BISHOPS_QUEENS]) ||
            (GetRookMoves(board->allPieceTables, square) & opponentPieceTables[PIECE_TABLE_ROOKS_QUEENS]) ||
            (GetKingMoves(square) & SquareEncode(board->playerToMove == White ? board->blackKingSquare : board->whiteKingSquare)))
        {
            // Subtract off the piece's value itself. This will cause prioritization of capturing
            // high value pieces with low value pieces (MVV/LVA).
            approxScores[i] -= pieceToSquareValue;
        }
    }

#if MOVE_ORDERER_PRE_SORT
    // Simple sort based on approximate score.
    // We assume the number of moves is generally low enough that we do a selection sort instead of a merge sort or quick sort.
    // This sort is done in-place with a minimal amount of swapping.
    for (uint8_t i = 0; i < numMoves; ++i)
    {
        uint8_t swapPoint = i;
        int32_t highestScore = approxScores[i];
        for (uint8_t j = i + 1; j < numMoves; ++j)
        {
            if (approxScores[j] > highestScore)
            {
                swapPoint = j;
                highestScore = approxScores[j];
            }
        }

        if (swapPoint != i)
        {
            int32_t x = approxScores[i];
            approxScores[i] = approxScores[swapPoint];
            approxScores[swapPoint] = x;

            Move m = moves[i];
            moves[i] = moves[swapPoint];
            moves[swapPoint] = m;
        }
    }
#endif

    moveOrderer->moves = moves;
    moveOrderer->approxScores = approxScores;
    moveOrderer->numMoves = numMoves;
    moveOrderer->curIndex = 0;
}

bool MoveOrdererGetNextMove(MoveOrderer * moveOrderer, Move * move)
{
    // This is a weird hybrid sorting interator thing.
    // The idea is that we don't need/want to sort the entire list up front because if we prune heavily then we sorted a bunch for no reason.
    // So we only sort one element at a time while iterating.
    // We assume the number of moves is generally low enough that we do a selection sort instead of a merge sort or quick sort.
    // This sort is done in-place with a minimal amount of swapping.
    if (moveOrderer->curIndex >= moveOrderer->numMoves)
        return false;

#if !MOVE_ORDERER_PRE_SORT
    // Start at the current index, which is the first candidate move, and iterate to the end, checking for the best score.
    uint8_t swapPoint = moveOrderer->curIndex;
    int32_t highestScore = moveOrderer->approxScores[moveOrderer->curIndex];
    for (uint8_t i = moveOrderer->curIndex + 1; i < moveOrderer->numMoves; ++i)
    {
        if (moveOrderer->approxScores[i] > highestScore)
        {
            swapPoint = i;
            highestScore = moveOrderer->approxScores[i];
        }
    }

    // Shuffle the best move to the front, and then advance curIndex so we don't iterate over it again.
    if (swapPoint != moveOrderer->curIndex)
    {
        int32_t x = moveOrderer->approxScores[moveOrderer->curIndex];
        moveOrderer->approxScores[moveOrderer->curIndex] = moveOrderer->approxScores[swapPoint];
        moveOrderer->approxScores[swapPoint] = x;

        Move m = moveOrderer->moves[moveOrderer->curIndex];
        moveOrderer->moves[moveOrderer->curIndex] = moveOrderer->moves[swapPoint];
        moveOrderer->moves[swapPoint] = m;
    }
#endif

    *move = moveOrderer->moves[moveOrderer->curIndex];
    moveOrderer->curIndex++;
    return true;
}

void MoveOrdererPrint(const MoveOrderer * moveOrderer)
{
#if !MOVE_ORDERER_PRE_SORT
    // Simple sort based on approximate score.
    // We assume the number of moves is generally low enough that we do a selection sort instead of a merge sort or quick sort.
    // This sort is done in-place with a minimal amount of swapping.
    for (uint8_t i = 0; i < moveOrderer->numMoves; ++i)
    {
        uint8_t swapPoint = i;
        int32_t highestScore = moveOrderer->approxScores[i];
        for (uint8_t j = i + 1; j < moveOrderer->numMoves; ++j)
        {
            if (moveOrderer->approxScores[j] > highestScore)
            {
                swapPoint = j;
                highestScore = moveOrderer->approxScores[j];
            }
        }

        if (swapPoint != i)
        {
            int32_t x = moveOrderer->approxScores[i];
            moveOrderer->approxScores[i] = moveOrderer->approxScores[swapPoint];
            moveOrderer->approxScores[swapPoint] = x;

            Move m = moveOrderer->moves[i];
            moveOrderer->moves[i] = moveOrderer->moves[swapPoint];
            moveOrderer->moves[swapPoint] = m;
        }
    }
#endif

    char moveStr[6]; // Max 5 chars plus extra character for null-termination
    for (uint8_t i = 0; i < moveOrderer->numMoves; ++i)
    {
        memset(moveStr, 0, sizeof(moveStr));
        if (0 != MoveToString(moveOrderer->moves[i], moveStr, sizeof(moveStr) - 1))
        {
            if (i > 0)
                putc(' ', stdout);
            printf(moveStr); // No puts here... puts adds a newline.
        }
    }
    printf("\n");
    fflush(stdout);
}
