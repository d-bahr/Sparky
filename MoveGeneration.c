#include "Board.h"
#include "Intrinsics.h"
#include "Move.h"
#include "MoveGeneration.h"
#include "Player.h"
#include "Square.h"
#include "StaticEval.h"
#include "Zobrist.h"

#include "tables/InBetweenMasks.h"
#include "tables/MoveTables.h"
#include "tables/PinIndices.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define U64_MASK_ALL 0xFFFFFFFFFFFFFFFFull

typedef struct
{
    uint64_t pinMovementMask[8]; // This is exactly one cache line on most systems. This is put first so the cache line isn't interleaved with other more-commonly-used data.
    uint64_t checkDefenseMask;
    uint64_t pinnedMask;
    const Board * board;
    const uint64_t * friendlyPieceTables;
    const uint64_t * opponentPieceTables;
    const uint64_t * friendlyShortPawnMoves;
    const uint64_t * friendlyLongPawnMoves;
    const uint64_t * friendlyPawnAttacks;
    Player player;
    Square friendlyKingSquare;
    Square opponentKingSquare;
} MoveContext;

static uint64_t attackTables[8];

static FORCE_INLINE EncodedSquare GetValidWhitePawnMoves(const MoveContext * moveContext, Square square, bool capturesOnly)
{
    EncodedSquare validMoves = 0;
    uint64_t enPassantMask = SquareEncode(moveContext->board->enPassantSquare);

    if (!capturesOnly)
    {
        // Move forward one square.
        validMoves |= intrinsic_andn64(moveContext->board->allPieceTables, moveContext->friendlyShortPawnMoves[square]);

        // Move forward two squares.
        validMoves |= intrinsic_andn64(moveContext->board->allPieceTables | moveContext->board->allPieceTables << 8, moveContext->friendlyLongPawnMoves[square]);
    }

    // Captures.
    validMoves |= GetPawnCaptureMoves(moveContext->friendlyPawnAttacks, square) & (moveContext->opponentPieceTables[PIECE_TABLE_COMBINED] | enPassantMask);

    // Special case for en-passant capture which removes a checking pawn.
    uint64_t mask = (moveContext->checkDefenseMask & moveContext->opponentPieceTables[PIECE_TABLE_PAWNS]) << 8;
    validMoves &= moveContext->checkDefenseMask | (mask & enPassantMask);

    if (validMoves && (SquareEncode(square) & moveContext->pinnedMask))
        validMoves &= moveContext->pinMovementMask[s_pinIndices[moveContext->friendlyKingSquare][square]];

    // Special case to handle en-passant which would reveal an attack on the king. This is a weird case
    // where the pawn is pinned from doing en-passant, but not pinned for other moves. There's not really
    // a clever way to handle this, but en-passant is relatively rare and only potentially applies for
    // a single pawn per ply, so a one-off bit of extra logic here seems reasonable.
    if (validMoves & enPassantMask)
    {
        // This edge case only happens when the king and pawn are on the same file. So we only need to check for rook moves, not bishop moves.
        assert(((enPassantMask >> 8) & moveContext->board->allPieceTables) != 0);
        uint64_t mask = (enPassantMask >> 8) | (enPassantMask | SquareEncode(square)); // Mask in the "to" and "from" squares.
        if (GetRookMoves(moveContext->board->allPieceTables ^ mask, moveContext->friendlyKingSquare) & moveContext->opponentPieceTables[PIECE_TABLE_ROOKS_QUEENS])
            validMoves &= ~enPassantMask; // en-passant is not possible due to revealed pin
    }

    return validMoves;
}

static FORCE_INLINE EncodedSquare GetValidBlackPawnMoves(const MoveContext * moveContext, Square square, bool capturesOnly)
{
    EncodedSquare validMoves = 0;
    uint64_t enPassantMask = SquareEncode(moveContext->board->enPassantSquare);

    if (!capturesOnly)
    {
        // Move forward one square.
        validMoves |= intrinsic_andn64(moveContext->board->allPieceTables, moveContext->friendlyShortPawnMoves[square]);

        // Move forward two squares.
        validMoves |= intrinsic_andn64(moveContext->board->allPieceTables | moveContext->board->allPieceTables >> 8, moveContext->friendlyLongPawnMoves[square]);
    }

    // Captures.
    validMoves |= GetPawnCaptureMoves(moveContext->friendlyPawnAttacks, square) & (moveContext->opponentPieceTables[PIECE_TABLE_COMBINED] | enPassantMask);

    // Special case for en-passant capture which removes a checking pawn.
    uint64_t mask = (moveContext->checkDefenseMask & moveContext->opponentPieceTables[PIECE_TABLE_PAWNS]) >> 8;
    validMoves &= moveContext->checkDefenseMask | (mask & enPassantMask);

    if (validMoves && (SquareEncode(square) & moveContext->pinnedMask))
        validMoves &= moveContext->pinMovementMask[s_pinIndices[moveContext->friendlyKingSquare][square]];

    // Special case to handle en-passant which would reveal an attack on the king. This is a weird case
    // where the pawn is pinned from doing en-passant, but not pinned for other moves. There's not really
    // a clever way to handle this, but en-passant is relatively rare and only potentially applies for
    // a single pawn per ply, so a one-off bit of extra logic here seems reasonable.
    if (validMoves & enPassantMask)
    {
        // This edge case only happens when the king and pawn are on the same file. So we only need to check for rook moves, not bishop moves.
        assert(((enPassantMask << 8) & moveContext->board->allPieceTables) != 0);
        uint64_t mask = (enPassantMask << 8) | (enPassantMask | SquareEncode(square)); // Mask in the "to" and "from" squares.
        if (GetRookMoves(moveContext->board->allPieceTables ^ mask, moveContext->friendlyKingSquare) & moveContext->opponentPieceTables[PIECE_TABLE_ROOKS_QUEENS])
            validMoves &= ~enPassantMask; // en-passant is not possible due to revealed pin
    }

    return validMoves;
}

static FORCE_INLINE EncodedSquare GetPseudoLegalWhitePawnMoves(const MoveContext * moveContext, Square square)
{
    // Move forward one square.
    EncodedSquare validMoves = intrinsic_andn64(moveContext->board->allPieceTables, moveContext->friendlyShortPawnMoves[square]);

    // Move forward two squares.
    validMoves |= intrinsic_andn64(moveContext->board->allPieceTables | moveContext->board->allPieceTables << 8, moveContext->friendlyLongPawnMoves[square]);

    // Captures (including en passant).
    validMoves |= GetPawnCaptureMoves(moveContext->friendlyPawnAttacks, square) & (moveContext->opponentPieceTables[PIECE_TABLE_COMBINED] | SquareEncode(moveContext->board->enPassantSquare));

    return validMoves;
}

static FORCE_INLINE EncodedSquare GetPseudoLegalBlackPawnMoves(const MoveContext * moveContext, Square square)
{
    // Move forward one square.
    EncodedSquare validMoves = intrinsic_andn64(moveContext->board->allPieceTables, moveContext->friendlyShortPawnMoves[square]);

    // Move forward two squares.
    validMoves |= intrinsic_andn64(moveContext->board->allPieceTables | moveContext->board->allPieceTables >> 8, moveContext->friendlyLongPawnMoves[square]);

    // Captures (including en passant).
    validMoves |= GetPawnCaptureMoves(moveContext->friendlyPawnAttacks, square) & (moveContext->opponentPieceTables[PIECE_TABLE_COMBINED] | SquareEncode(moveContext->board->enPassantSquare));

    return validMoves;
}

static FORCE_INLINE EncodedSquare GetPseudoLegalPawnCaptures(const MoveContext * moveContext, Square square)
{
    return GetPawnCaptureMoves(moveContext->friendlyPawnAttacks, square) & (moveContext->opponentPieceTables[PIECE_TABLE_COMBINED] | SquareEncode(moveContext->board->enPassantSquare));
}

static FORCE_INLINE EncodedSquare GetValidKnightMoves(const MoveContext * moveContext, Square square)
{
    return intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED], GetKnightMoves(square));
}

static FORCE_INLINE EncodedSquare GetValidKnightCaptures(const MoveContext * moveContext, Square square)
{
    return moveContext->opponentPieceTables[PIECE_TABLE_COMBINED] & GetKnightMoves(square);
}

static FORCE_INLINE EncodedSquare GetValidBishopMoves(const MoveContext * moveContext, Square square)
{
    return intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED], GetBishopMoves(moveContext->board->allPieceTables, square));
}

static FORCE_INLINE EncodedSquare GetValidBishopCaptures(const MoveContext * moveContext, Square square)
{
    return moveContext->opponentPieceTables[PIECE_TABLE_COMBINED] & GetBishopMoves(moveContext->board->allPieceTables, square);
}

static FORCE_INLINE EncodedSquare GetValidRookMoves(const MoveContext * moveContext, Square square)
{
    return intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED], GetRookMoves(moveContext->board->allPieceTables, square));
}

static FORCE_INLINE EncodedSquare GetValidRookCaptures(const MoveContext * moveContext, Square square)
{
    return moveContext->opponentPieceTables[PIECE_TABLE_COMBINED] & GetRookMoves(moveContext->board->allPieceTables, square);
}

static FORCE_INLINE EncodedSquare GetValidQueenMoves(const MoveContext * moveContext, Square square)
{
    return intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED], GetQueenMoves(moveContext->board->allPieceTables, square));
}

static FORCE_INLINE EncodedSquare GetValidQueenCaptures(const MoveContext * moveContext, Square square)
{
    return moveContext->opponentPieceTables[PIECE_TABLE_COMBINED] & GetQueenMoves(moveContext->board->allPieceTables, square);
}

// TODO: Bitwise-or or logical-or?
static bool SquareIsAttacked(const MoveContext * moveContext, Square square)
{
    return (GetRookMoves(moveContext->board->allPieceTables, square) & moveContext->opponentPieceTables[PIECE_TABLE_ROOKS_QUEENS]) |
        (GetBishopMoves(moveContext->board->allPieceTables, square) & moveContext->opponentPieceTables[PIECE_TABLE_BISHOPS_QUEENS]) |
        (GetKnightMoves(square) & moveContext->opponentPieceTables[PIECE_TABLE_KNIGHTS]) |
        (GetPawnCaptureMoves(moveContext->friendlyPawnAttacks, square) & moveContext->opponentPieceTables[PIECE_TABLE_PAWNS]) |
        (GetKingMoves(square) & SquareEncode(moveContext->opponentKingSquare));
}

static bool KingPseudoLegalSquareIsAttacked(const MoveContext * moveContext, Square square)
{
    // Remove the king's current position from the rook and bishop line attacks.
    // Otherwise the line attack would hit the king's "old" position and not continue past.
    // Failure to capture this edge case means that the king could "back up" (relative to the checking piece) into a square that is still in check.
    EncodedSquare friendlyKingMask = SquareEncode(moveContext->friendlyKingSquare);
    return (GetRookMoves(moveContext->board->allPieceTables ^ friendlyKingMask, square) & moveContext->opponentPieceTables[PIECE_TABLE_ROOKS_QUEENS]) |
        (GetBishopMoves(moveContext->board->allPieceTables ^ friendlyKingMask, square) & moveContext->opponentPieceTables[PIECE_TABLE_BISHOPS_QUEENS]) |
        (GetKnightMoves(square) & moveContext->opponentPieceTables[PIECE_TABLE_KNIGHTS]) |
        (GetPawnCaptureMoves(moveContext->friendlyPawnAttacks, square) & moveContext->opponentPieceTables[PIECE_TABLE_PAWNS]) |
        (GetKingMoves(square) & SquareEncode(moveContext->opponentKingSquare));
}

static FORCE_INLINE EncodedSquare GetValidWhiteKingMoves(const MoveContext * moveContext, Square square, uint8_t numChecks)
{
    EncodedSquare validMoves = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED], GetKingMoves(square));

    // Remove moves which are attacked by the opponent.
    EncodedSquare tempMoves = validMoves;
    while (tempMoves)
    {
        square = SquareDecodeLowest(tempMoves);
        if (KingPseudoLegalSquareIsAttacked(moveContext, square))
            validMoves ^= SquareEncode(square);
        tempMoves = intrinsic_blsr64(tempMoves);
    }

    // Allow castling if not in check.
    if (numChecks == 0)
    {
        if (moveContext->board->castleBits & WhiteShortCastle)
        {
            if ((moveContext->board->allPieceTables & 0x0000000000000060) == 0 && !SquareIsAttacked(moveContext, SquareF1) && !SquareIsAttacked(moveContext, SquareG1))
                validMoves |= 0x0000000000000040;
        }

        if (moveContext->board->castleBits & WhiteLongCastle)
        {
            if ((moveContext->board->allPieceTables & 0x000000000000000e) == 0 && !SquareIsAttacked(moveContext, SquareC1) && !SquareIsAttacked(moveContext, SquareD1))
                validMoves |= 0x0000000000000004;
        }
    }

    return validMoves;
}

static FORCE_INLINE EncodedSquare GetValidBlackKingMoves(const MoveContext * moveContext, Square square, uint8_t numChecks)
{
    EncodedSquare validMoves = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED], GetKingMoves(square));

    // Remove moves which are attacked by the opponent.
    EncodedSquare tempMoves = validMoves;
    while (tempMoves)
    {
        square = SquareDecodeLowest(tempMoves);
        if (KingPseudoLegalSquareIsAttacked(moveContext, square))
            validMoves ^= SquareEncode(square);
        tempMoves = intrinsic_blsr64(tempMoves);
    }

    // Allow castling if not in check.
    if (numChecks == 0)
    {
        if (moveContext->board->castleBits & BlackShortCastle)
        {
            if ((moveContext->board->allPieceTables & 0x6000000000000000) == 0 && !SquareIsAttacked(moveContext, SquareF8) && !SquareIsAttacked(moveContext, SquareG8))
                validMoves |= 0x4000000000000000;
        }

        if (moveContext->board->castleBits & BlackLongCastle)
        {
            if ((moveContext->board->allPieceTables & 0x0e00000000000000) == 0 && !SquareIsAttacked(moveContext, SquareC8) && !SquareIsAttacked(moveContext, SquareD8))
                validMoves |= 0x0400000000000000;
        }
    }

    return validMoves;
}

static FORCE_INLINE EncodedSquare GetWhiteKingPseudoLegalMoves(const MoveContext * moveContext, Square square)
{
    EncodedSquare validMoves = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED], GetKingMoves(square));

    if (moveContext->board->castleBits & WhiteShortCastle)
    {
        if ((moveContext->board->allPieceTables & 0x0000000000000060) == 0 && !SquareIsAttacked(moveContext, SquareF1) && !SquareIsAttacked(moveContext, SquareG1))
            validMoves |= 0x0000000000000040;
    }

    if (moveContext->board->castleBits & WhiteLongCastle)
    {
        if ((moveContext->board->allPieceTables & 0x000000000000000e) == 0 && !SquareIsAttacked(moveContext, SquareC1) && !SquareIsAttacked(moveContext, SquareD1))
            validMoves |= 0x0000000000000004;
    }

    return validMoves;
}

static FORCE_INLINE EncodedSquare GetBlackKingPseudoLegalMoves(const MoveContext * moveContext, Square square)
{
    EncodedSquare validMoves = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED], GetKingMoves(square));

    if (moveContext->board->castleBits & BlackShortCastle)
    {
        if ((moveContext->board->allPieceTables & 0x6000000000000000) == 0 && !SquareIsAttacked(moveContext, SquareF8) && !SquareIsAttacked(moveContext, SquareG8))
            validMoves |= 0x4000000000000000;
    }

    if (moveContext->board->castleBits & BlackLongCastle)
    {
        if ((moveContext->board->allPieceTables & 0x0e00000000000000) == 0 && !SquareIsAttacked(moveContext, SquareC8) && !SquareIsAttacked(moveContext, SquareD8))
            validMoves |= 0x0400000000000000;
    }

    return validMoves;
}

static FORCE_INLINE EncodedSquare GetKingPseudoLegalCaptures(const MoveContext * moveContext, Square square)
{
    return moveContext->opponentPieceTables[PIECE_TABLE_COMBINED] & GetKingMoves(square);
}

static FORCE_INLINE EncodedSquare GetValidKingCaptures(const MoveContext * moveContext, Square square)
{
    EncodedSquare validMoves = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED], GetKingMoves(square) & moveContext->opponentPieceTables[PIECE_TABLE_COMBINED]);

    // Remove moves which are attacked by the opponent.
    EncodedSquare tempMoves = validMoves;
    while (tempMoves)
    {
        square = SquareDecodeLowest(tempMoves);
        if (KingPseudoLegalSquareIsAttacked(moveContext, square))
            validMoves ^= SquareEncode(square);
        tempMoves = intrinsic_blsr64(tempMoves);
    }

    return validMoves;
}

static FORCE_INLINE EncodedSquare MaskPinsAndChecks(const MoveContext * moveContext, Square square, EncodedSquare moves)
{
    moves &= moveContext->checkDefenseMask; // Handle defense for checks.

    if (moves && (SquareEncode(square) & moveContext->pinnedMask))
        moves &= moveContext->pinMovementMask[s_pinIndices[moveContext->friendlyKingSquare][square]];

    return moves;
}

static uint8_t GetAllValidWhitePawnMoves(const MoveContext * moveContext, Move * moves, bool capturesOnly)
{
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_PAWNS];
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        EncodedSquare encodedMoves = GetValidWhitePawnMoves(moveContext, targetSquare, capturesOnly);
        while (encodedMoves != 0)
        {
            Square toSquare = SquareDecodeLowest(encodedMoves);

            moves[moveCounter].to = toSquare;
            moves[moveCounter].from = targetSquare;
            moves[moveCounter].piece = Pawn;

            if (SquareGetRank(toSquare) == Rank8)
            {
                // This is manually unrolled for speed.
                moves[moveCounter].promotion = Queen;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Rook;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Knight;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Bishop;
            }
            else
            {
                // Need consistent value here for transposition tables, because this will get hashed
                // even when there is no promotion.
                moves[moveCounter].promotion = None;
            }

            encodedMoves = intrinsic_blsr64(encodedMoves);
            moveCounter++;
        }

        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static uint8_t GetAllValidBlackPawnMoves(const MoveContext * moveContext, Move * moves, bool capturesOnly)
{
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_PAWNS];
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        EncodedSquare encodedMoves = GetValidBlackPawnMoves(moveContext, targetSquare, capturesOnly);
        while (encodedMoves != 0)
        {
            Square toSquare = SquareDecodeLowest(encodedMoves);

            moves[moveCounter].to = toSquare;
            moves[moveCounter].from = targetSquare;
            moves[moveCounter].piece = Pawn;

            if (SquareGetRank(toSquare) == Rank1)
            {
                // This is manually unrolled for speed.
                moves[moveCounter].promotion = Queen;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Rook;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Knight;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Bishop;
            }
            else
            {
                // Need consistent value here for transposition tables, because this will get hashed
                // even when there is no promotion.
                moves[moveCounter].promotion = None;
            }

            encodedMoves = intrinsic_blsr64(encodedMoves);
            moveCounter++;
        }

        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static uint8_t HasValidWhitePawnMove(const MoveContext * moveContext)
{
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_PAWNS];
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        EncodedSquare encodedMoves = GetValidWhitePawnMoves(moveContext, targetSquare, false);
        if (encodedMoves > 0)
            return true;
        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return false;
}

static uint8_t GetNumValidWhitePawnMoves(const MoveContext * moveContext)
{
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_PAWNS];
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        EncodedSquare encodedMoves = GetValidWhitePawnMoves(moveContext, targetSquare, false);
        moveCounter += (uint8_t) intrinsic_popcnt64(encodedMoves);
        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static uint8_t HasValidBlackPawnMove(const MoveContext * moveContext)
{
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_PAWNS];
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        EncodedSquare encodedMoves = GetValidBlackPawnMoves(moveContext, targetSquare, false);
        if (encodedMoves > 0)
            return true;
        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return false;
}

static uint8_t GetNumValidBlackPawnMoves(const MoveContext * moveContext)
{
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_PAWNS];
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        EncodedSquare encodedMoves = GetValidBlackPawnMoves(moveContext, targetSquare, false);
        moveCounter += (uint8_t) intrinsic_popcnt64(encodedMoves);
        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static uint8_t GetAllPseudoLegalWhitePawnMoves(const MoveContext * moveContext, Move * moves)
{
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_PAWNS];
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        EncodedSquare encodedMoves = GetPseudoLegalWhitePawnMoves(moveContext, targetSquare);
        while (encodedMoves != 0)
        {
            Square toSquare = SquareDecodeLowest(encodedMoves);

            moves[moveCounter].to = toSquare;
            moves[moveCounter].from = targetSquare;
            moves[moveCounter].piece = Pawn;

            if (SquareGetRank(toSquare) == Rank8)
            {
                // This is manually unrolled for speed.
                moves[moveCounter].promotion = Queen;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Rook;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Knight;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Bishop;
            }
            else
            {
                // Need consistent value here for transposition tables, because this will get hashed
                // even when there is no promotion.
                moves[moveCounter].promotion = None;
            }

            encodedMoves = intrinsic_blsr64(encodedMoves);
            moveCounter++;
        }

        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static uint8_t GetAllPseudoLegalBlackPawnMoves(const MoveContext * moveContext, Move * moves)
{
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_PAWNS];
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        EncodedSquare encodedMoves = GetPseudoLegalBlackPawnMoves(moveContext, targetSquare);
        while (encodedMoves != 0)
        {
            Square toSquare = SquareDecodeLowest(encodedMoves);

            moves[moveCounter].to = toSquare;
            moves[moveCounter].from = targetSquare;
            moves[moveCounter].piece = Pawn;

            if (SquareGetRank(toSquare) == Rank1)
            {
                // This is manually unrolled for speed.
                moves[moveCounter].promotion = Queen;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Rook;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Knight;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Bishop;
            }
            else
            {
                // Need consistent value here for transposition tables, because this will get hashed
                // even when there is no promotion.
                moves[moveCounter].promotion = None;
            }

            encodedMoves = intrinsic_blsr64(encodedMoves);
            moveCounter++;
        }

        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static uint8_t GetAllPseudoLegalPawnCaptures(const MoveContext * moveContext, Move * moves, Rank promotionRank)
{
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_PAWNS];
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        EncodedSquare encodedMoves = GetPseudoLegalPawnCaptures(moveContext, targetSquare);
        while (encodedMoves != 0)
        {
            Square toSquare = SquareDecodeLowest(encodedMoves);

            moves[moveCounter].to = toSquare;
            moves[moveCounter].from = targetSquare;
            moves[moveCounter].piece = Pawn;

            if (SquareGetRank(toSquare) == promotionRank)
            {
                // This is manually unrolled for speed.
                moves[moveCounter].promotion = Queen;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Rook;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Knight;

                moveCounter++;
                moves[moveCounter] = moves[moveCounter - 1];
                moves[moveCounter].promotion = Bishop;
            }
            else
            {
                // Need consistent value here for transposition tables, because this will get hashed
                // even when there is no promotion.
                moves[moveCounter].promotion = None;
            }

            encodedMoves = intrinsic_blsr64(encodedMoves);
            moveCounter++;
        }

        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static FORCE_INLINE bool HasValidNonPawnNonKingMove(const MoveContext * moveContext, PieceType pieceType)
{
    typedef EncodedSquare(*PieceMoveFn)(const MoveContext *, Square);

    PieceMoveFn getPieceMoves;
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable;
    switch (pieceType)
    {
    case Knight:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_KNIGHTS];
        getPieceMoves = GetValidKnightMoves;
        break;
    case Bishop:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS]);
        getPieceMoves = GetValidBishopMoves;
        break;
    case Rook:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS]);
        getPieceMoves = GetValidRookMoves;
        break;
    case Queen:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS] & moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS];
        getPieceMoves = GetValidQueenMoves;
        break;
    default:
        return 0;
    }
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        // Unlike the case for pawns, using a switch statement here is actually ~5-10% slower than a function pointer lookup, even with the extra stack frame.
        EncodedSquare encodedMoves = getPieceMoves(moveContext, targetSquare);
        encodedMoves = MaskPinsAndChecks(moveContext, targetSquare, encodedMoves);
        if (encodedMoves > 0)
            return true;
        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return false;
}

static FORCE_INLINE uint8_t GetNumValidNonPawnNonKingMoves(const MoveContext * moveContext, PieceType pieceType)
{
    typedef EncodedSquare(*PieceMoveFn)(const MoveContext *, Square);

    PieceMoveFn getPieceMoves;
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable;
    switch (pieceType)
    {
    case Knight:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_KNIGHTS];
        getPieceMoves = GetValidKnightMoves;
        break;
    case Bishop:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS]);
        getPieceMoves = GetValidBishopMoves;
        break;
    case Rook:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS]);
        getPieceMoves = GetValidRookMoves;
        break;
    case Queen:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS] & moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS];
        getPieceMoves = GetValidQueenMoves;
        break;
    default:
        return 0;
    }
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        // Unlike the case for pawns, using a switch statement here is actually ~5-10% slower than a function pointer lookup, even with the extra stack frame.
        EncodedSquare encodedMoves = getPieceMoves(moveContext, targetSquare);
        encodedMoves = MaskPinsAndChecks(moveContext, targetSquare, encodedMoves);
        moveCounter += (uint8_t) intrinsic_popcnt64(encodedMoves);
        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static FORCE_INLINE uint8_t GetAllValidNonPawnNonKingMoves(const MoveContext * moveContext, PieceType pieceType, Move * moves)
{
    typedef EncodedSquare(*PieceMoveFn)(const MoveContext *, Square);

    PieceMoveFn getPieceMoves;
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable;
    switch (pieceType)
    {
    case Knight:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_KNIGHTS];
        getPieceMoves = GetValidKnightMoves;
        break;
    case Bishop:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS]);
        getPieceMoves = GetValidBishopMoves;
        break;
    case Rook:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS]);
        getPieceMoves = GetValidRookMoves;
        break;
    case Queen:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS] & moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS];
        getPieceMoves = GetValidQueenMoves;
        break;
    default:
        return 0;
    }
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        // Unlike the case for pawns, using a switch statement here is actually ~5-10% slower than a function pointer lookup, even with the extra stack frame.
        EncodedSquare encodedMoves = getPieceMoves(moveContext, targetSquare);
        encodedMoves = MaskPinsAndChecks(moveContext, targetSquare, encodedMoves);
        while (encodedMoves)
        {
            moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
            moves[moveCounter].from = targetSquare;
            moves[moveCounter].piece = pieceType;
            moves[moveCounter].promotion = None;
            moveCounter++;
            encodedMoves = intrinsic_blsr64(encodedMoves);
        }

        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static FORCE_INLINE uint8_t GetAllValidNonPawnNonKingCaptures(const MoveContext * moveContext, PieceType pieceType, Move * moves)
{
    typedef EncodedSquare(*PieceMoveFn)(const MoveContext *, Square);

    PieceMoveFn getPieceMoves;
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable;
    switch (pieceType)
    {
    case Knight:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_KNIGHTS];
        getPieceMoves = GetValidKnightCaptures;
        break;
    case Bishop:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS]);
        getPieceMoves = GetValidBishopCaptures;
        break;
    case Rook:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS]);
        getPieceMoves = GetValidRookCaptures;
        break;
    case Queen:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS] & moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS];
        getPieceMoves = GetValidQueenCaptures;
        break;
    default:
        return 0;
    }
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        // Unlike the case for pawns, using a switch statement here is actually ~5-10% slower than a function pointer lookup, even with the extra stack frame.
        EncodedSquare encodedMoves = getPieceMoves(moveContext, targetSquare);
        encodedMoves = MaskPinsAndChecks(moveContext, targetSquare, encodedMoves);
        while (encodedMoves)
        {
            moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
            moves[moveCounter].from = targetSquare;
            moves[moveCounter].piece = pieceType;
            moves[moveCounter].promotion = None;
            moveCounter++;
            encodedMoves = intrinsic_blsr64(encodedMoves);
        }

        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static FORCE_INLINE uint8_t GetPseudoLegalNonPawnNonKingMoves(const MoveContext * moveContext, PieceType pieceType, Move * moves)
{
    typedef EncodedSquare(*PieceMoveFn)(const MoveContext *, Square);

    PieceMoveFn getPieceMoves;
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable;
    switch (pieceType)
    {
    case Knight:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_KNIGHTS];
        getPieceMoves = GetValidKnightMoves;
        break;
    case Bishop:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS]);
        getPieceMoves = GetValidBishopMoves;
        break;
    case Rook:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS]);
        getPieceMoves = GetValidRookMoves;
        break;
    case Queen:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS] & moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS];
        getPieceMoves = GetValidQueenMoves;
        break;
    default:
        return 0;
    }
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        // Unlike the case for pawns, using a switch statement here is actually ~5-10% slower than a function pointer lookup, even with the extra stack frame.
        EncodedSquare encodedMoves = getPieceMoves(moveContext, targetSquare);
        while (encodedMoves)
        {
            moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
            moves[moveCounter].from = targetSquare;
            moves[moveCounter].piece = pieceType;
            moves[moveCounter].promotion = None;
            moveCounter++;
            encodedMoves = intrinsic_blsr64(encodedMoves);
        }

        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static FORCE_INLINE uint8_t GetPseudoLegalNonPawnNonKingCaptures(const MoveContext * moveContext, PieceType pieceType, Move * moves)
{
    typedef EncodedSquare(*PieceMoveFn)(const MoveContext *, Square);

    PieceMoveFn getPieceMoves;
    uint8_t moveCounter = 0;
    uint64_t temporaryPieceTable;
    switch (pieceType)
    {
    case Knight:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_KNIGHTS];
        getPieceMoves = GetValidKnightCaptures;
        break;
    case Bishop:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS]);
        getPieceMoves = GetValidBishopCaptures;
        break;
    case Rook:
        temporaryPieceTable = intrinsic_andn64(moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS], moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS]);
        getPieceMoves = GetValidRookCaptures;
        break;
    case Queen:
        temporaryPieceTable = moveContext->friendlyPieceTables[PIECE_TABLE_ROOKS_QUEENS] & moveContext->friendlyPieceTables[PIECE_TABLE_BISHOPS_QUEENS];
        getPieceMoves = GetValidQueenCaptures;
        break;
    default:
        return 0;
    }
    while (temporaryPieceTable != 0)
    {
        Square targetSquare = SquareDecodeLowest(temporaryPieceTable);
        // Unlike the case for pawns, using a switch statement here is actually ~5-10% slower than a function pointer lookup, even with the extra stack frame.
        EncodedSquare encodedMoves = getPieceMoves(moveContext, targetSquare);
        while (encodedMoves)
        {
            moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
            moves[moveCounter].from = targetSquare;
            moves[moveCounter].piece = pieceType;
            moves[moveCounter].promotion = None;
            moveCounter++;
            encodedMoves = intrinsic_blsr64(encodedMoves);
        }

        temporaryPieceTable = intrinsic_blsr64(temporaryPieceTable);
    }
    return moveCounter;
}

static FORCE_INLINE uint8_t GetCheckDefenseMask(MoveContext * moveContext)
{
    uint8_t numChecks = 0;
    const uint64_t * kingToPieceMask = s_inBetweenMask[moveContext->friendlyKingSquare];
    moveContext->checkDefenseMask = U64_MASK_ALL;
    moveContext->pinnedMask = 0;

    EncodedSquare rookMovesFromKing = GetRookMoves(moveContext->board->allPieceTables, moveContext->friendlyKingSquare);
    EncodedSquare opponentRooksQueens = moveContext->opponentPieceTables[PIECE_TABLE_ROOKS_QUEENS];
    EncodedSquare rookQueenAttackers = rookMovesFromKing & opponentRooksQueens;
    if (rookQueenAttackers)
    {
        moveContext->checkDefenseMask &= (rookQueenAttackers | kingToPieceMask[SquareDecodeLowest(rookQueenAttackers)]);
        numChecks += (uint8_t) intrinsic_popcnt64(rookQueenAttackers); // Note: multiple rook/queen checks are possible after a revealed attack from a pawn capture promotion to rook/queen.
    }

    EncodedSquare rookPinMask = rookMovesFromKing & moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED];
    if (rookPinMask)
    {
        EncodedSquare xrays = GetRookMoves(moveContext->board->allPieceTables ^ rookPinMask, moveContext->friendlyKingSquare) & intrinsic_andn64(rookMovesFromKing, opponentRooksQueens);
        while (xrays)
        {
            Square xray = SquareDecodeLowest(xrays);
            uint64_t mask = kingToPieceMask[xray];
            moveContext->pinnedMask |= mask & rookPinMask;
            moveContext->pinMovementMask[s_pinIndices[moveContext->friendlyKingSquare][xray]] = mask | SquareEncode(xray);
            xrays = intrinsic_blsr64(xrays);
        }
    }

    EncodedSquare bishopMovesFromKing = GetBishopMoves(moveContext->board->allPieceTables, moveContext->friendlyKingSquare);
    EncodedSquare opponentBishopsQueens = moveContext->opponentPieceTables[PIECE_TABLE_BISHOPS_QUEENS];
    EncodedSquare bishopQueenAttackers = bishopMovesFromKing & opponentBishopsQueens;
    if (bishopQueenAttackers)
    {
        moveContext->checkDefenseMask &= (bishopQueenAttackers | kingToPieceMask[SquareDecodeLowest(bishopQueenAttackers)]);
        numChecks += (uint8_t) intrinsic_popcnt64(bishopQueenAttackers);
    }

    EncodedSquare bishopPinMask = bishopMovesFromKing & moveContext->friendlyPieceTables[PIECE_TABLE_COMBINED];
    if (bishopPinMask)
    {
        EncodedSquare xrays = GetBishopMoves(moveContext->board->allPieceTables ^ bishopPinMask, moveContext->friendlyKingSquare) & intrinsic_andn64(bishopMovesFromKing, opponentBishopsQueens);
        while (xrays)
        {
            Square xray = SquareDecodeLowest(xrays);
            uint64_t mask = kingToPieceMask[xray];
            moveContext->pinnedMask |= mask & bishopPinMask;
            moveContext->pinMovementMask[s_pinIndices[moveContext->friendlyKingSquare][xray]] = mask | SquareEncode(xray);
            xrays = intrinsic_blsr64(xrays);
        }
    }

    EncodedSquare knightAttackers = GetKnightMoves(moveContext->friendlyKingSquare) & moveContext->opponentPieceTables[PIECE_TABLE_KNIGHTS];
    if (knightAttackers)
    {
        moveContext->checkDefenseMask &= knightAttackers;
        numChecks++;
    }

    EncodedSquare pawnAttackers = GetPawnCaptureMoves(moveContext->friendlyPawnAttacks, moveContext->friendlyKingSquare) & moveContext->opponentPieceTables[PIECE_TABLE_PAWNS];
    if (pawnAttackers)
    {
        moveContext->checkDefenseMask &= pawnAttackers;
        numChecks++;
    }

    return numChecks;
}

bool KingIsAttacked(const Board * board, Player playerOfKing)
{
    const uint64_t * opponentPieceTables;
    const uint64_t * friendlyPawnAttacks;
    Square kingSquare;
    if (playerOfKing == White)
    {
        opponentPieceTables = board->blackPieceTables;
        friendlyPawnAttacks = s_pawnAttackBitboardWhite;
        kingSquare = board->whiteKingSquare;
    }
    else
    {
        opponentPieceTables = board->whitePieceTables;
        friendlyPawnAttacks = s_pawnAttackBitboardBlack;
        kingSquare = board->blackKingSquare;
    }
    return (GetRookMoves(board->allPieceTables, kingSquare) & opponentPieceTables[PIECE_TABLE_ROOKS_QUEENS]) |
        (GetBishopMoves(board->allPieceTables, kingSquare) & opponentPieceTables[PIECE_TABLE_BISHOPS_QUEENS]) |
        (GetKnightMoves(kingSquare) & opponentPieceTables[PIECE_TABLE_KNIGHTS]) |
        (GetPawnCaptureMoves(friendlyPawnAttacks, kingSquare) & opponentPieceTables[PIECE_TABLE_PAWNS]);
}

bool IsStalemate(const Board * board)
{
    return !KingIsAttacked(board, board->playerToMove) && !HasMoves(board, board->playerToMove);
}

bool IsCheckmate(const Board * board)
{
    return KingIsAttacked(board, board->playerToMove) && !HasMoves(board, board->playerToMove);
}

bool HasMoves(const Board * board, Player player)
{
    MoveContext moveContext = { 0 };
    EncodedSquare encodedMoves;
    uint8_t numChecks;

    moveContext.board = board;
    moveContext.player = player;

    if (player == White)
    {
        moveContext.friendlyPieceTables = board->whitePieceTables;
        moveContext.friendlyKingSquare = board->whiteKingSquare;
        moveContext.opponentKingSquare = board->blackKingSquare;
        moveContext.opponentPieceTables = board->blackPieceTables;
        moveContext.friendlyShortPawnMoves = s_pawnShortMoveBitboardWhite;
        moveContext.friendlyLongPawnMoves = s_pawnLongMoveBitboardWhite;
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardWhite;
        numChecks = GetCheckDefenseMask(&moveContext);
        encodedMoves = GetValidWhiteKingMoves(&moveContext, moveContext.friendlyKingSquare, numChecks);

        if (encodedMoves != 0)
            return false;
        if (numChecks == 2) // In double check, only the king can move, but no moves are available, so must be a double-check checkmate.
            return true;

        if (HasValidWhitePawnMove(&moveContext))
            return false;
    }
    else
    {
        moveContext.friendlyPieceTables = board->blackPieceTables;
        moveContext.friendlyKingSquare = board->blackKingSquare;
        moveContext.opponentKingSquare = board->whiteKingSquare;
        moveContext.opponentPieceTables = board->whitePieceTables;
        moveContext.friendlyShortPawnMoves = s_pawnShortMoveBitboardBlack;
        moveContext.friendlyLongPawnMoves = s_pawnLongMoveBitboardBlack;
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardBlack;
        numChecks = GetCheckDefenseMask(&moveContext);
        encodedMoves = GetValidBlackKingMoves(&moveContext, moveContext.friendlyKingSquare, numChecks);

        if (encodedMoves != 0)
            return false;
        if (numChecks == 2) // In double check, only the king can move, but no moves are available, so must be a double-check checkmate.
            return true;

        if (HasValidBlackPawnMove(&moveContext))
            return false;
    }

    // Check the other pieces.
    if (HasValidNonPawnNonKingMove(&moveContext, Knight))
        return false;
    if (HasValidNonPawnNonKingMove(&moveContext, Bishop))
        return false;
    if (HasValidNonPawnNonKingMove(&moveContext, Rook))
        return false;
    if (HasValidNonPawnNonKingMove(&moveContext, Queen))
        return false;

    return true;
}

// Note: the maximum number of moves in a single position is thought to be 218. This has been discovered mostly by trial and error,
// and has not been conclusively proven, AFAIK. Therefore, in order to be extra safe, let's just allocate 256 moves.
uint8_t GetValidMoves(const Board * board, Move * moves)
{
    MoveContext moveContext = { 0 };
    EncodedSquare encodedMoves;
    uint8_t numChecks;
    uint8_t moveCounter = 0;

    moveContext.board = board;
    moveContext.player = board->playerToMove;

    if (board->playerToMove == White)
    {
        moveContext.friendlyPieceTables = board->whitePieceTables;
        moveContext.friendlyKingSquare = board->whiteKingSquare;
        moveContext.opponentKingSquare = board->blackKingSquare;
        moveContext.opponentPieceTables = board->blackPieceTables;
        moveContext.friendlyShortPawnMoves = s_pawnShortMoveBitboardWhite;
        moveContext.friendlyLongPawnMoves = s_pawnLongMoveBitboardWhite;
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardWhite;
        numChecks = GetCheckDefenseMask(&moveContext);
        encodedMoves = GetValidWhiteKingMoves(&moveContext, moveContext.friendlyKingSquare, numChecks);

        while (encodedMoves != 0)
        {
            moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
            moves[moveCounter].from = moveContext.friendlyKingSquare;
            moves[moveCounter].piece = King;
            moves[moveCounter].promotion = None;
            moveCounter++;
            encodedMoves = intrinsic_blsr64(encodedMoves);
        }
        if (numChecks == 2) // In double check, only the king can move.
            return moveCounter;

        moveCounter += GetAllValidWhitePawnMoves(&moveContext, &moves[moveCounter], false);
    }
    else
    {
        moveContext.friendlyPieceTables = board->blackPieceTables;
        moveContext.friendlyKingSquare = board->blackKingSquare;
        moveContext.opponentKingSquare = board->whiteKingSquare;
        moveContext.opponentPieceTables = board->whitePieceTables;
        moveContext.friendlyShortPawnMoves = s_pawnShortMoveBitboardBlack;
        moveContext.friendlyLongPawnMoves = s_pawnLongMoveBitboardBlack;
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardBlack;
        numChecks = GetCheckDefenseMask(&moveContext);
        encodedMoves = GetValidBlackKingMoves(&moveContext, moveContext.friendlyKingSquare, numChecks);

        while (encodedMoves != 0)
        {
            moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
            moves[moveCounter].from = moveContext.friendlyKingSquare;
            moves[moveCounter].piece = King;
            moves[moveCounter].promotion = None;
            moveCounter++;
            encodedMoves = intrinsic_blsr64(encodedMoves);
        }
        if (numChecks == 2) // In double check, only the king can move.
            return moveCounter;

        moveCounter += GetAllValidBlackPawnMoves(&moveContext, &moves[moveCounter], false);
    }

    // Check the other pieces.
    moveCounter += GetAllValidNonPawnNonKingMoves(&moveContext, Knight, &moves[moveCounter]);
    moveCounter += GetAllValidNonPawnNonKingMoves(&moveContext, Bishop, &moves[moveCounter]);
    moveCounter += GetAllValidNonPawnNonKingMoves(&moveContext, Rook, &moves[moveCounter]);
    moveCounter += GetAllValidNonPawnNonKingMoves(&moveContext, Queen, &moves[moveCounter]);

    return moveCounter;
}

// Gets valid moves which are captures. This is used for quiescence search.
uint8_t GetValidCaptures(const Board * board, Move * moves)
{
    MoveContext moveContext = { 0 };
    EncodedSquare encodedMoves;
    uint8_t numChecks;
    uint8_t moveCounter = 0;

    moveContext.board = board;
    moveContext.player = board->playerToMove;

    if (board->playerToMove == White)
    {
        moveContext.friendlyPieceTables = board->whitePieceTables;
        moveContext.friendlyKingSquare = board->whiteKingSquare;
        moveContext.opponentKingSquare = board->blackKingSquare;
        moveContext.opponentPieceTables = board->blackPieceTables;
        moveContext.friendlyShortPawnMoves = s_pawnShortMoveBitboardWhite;
        moveContext.friendlyLongPawnMoves = s_pawnLongMoveBitboardWhite;
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardWhite;
        numChecks = GetCheckDefenseMask(&moveContext);
        encodedMoves = GetValidKingCaptures(&moveContext, moveContext.friendlyKingSquare);

        while (encodedMoves != 0)
        {
            moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
            moves[moveCounter].from = moveContext.friendlyKingSquare;
            moves[moveCounter].piece = King;
            moves[moveCounter].promotion = None;
            moveCounter++;
            encodedMoves = intrinsic_blsr64(encodedMoves);
        }
        if (numChecks == 2) // In double check, only the king can move.
            return moveCounter;

        moveCounter += GetAllValidWhitePawnMoves(&moveContext, &moves[moveCounter], true);
    }
    else
    {
        moveContext.friendlyPieceTables = board->blackPieceTables;
        moveContext.friendlyKingSquare = board->blackKingSquare;
        moveContext.opponentKingSquare = board->whiteKingSquare;
        moveContext.opponentPieceTables = board->whitePieceTables;
        moveContext.friendlyShortPawnMoves = s_pawnShortMoveBitboardBlack;
        moveContext.friendlyLongPawnMoves = s_pawnLongMoveBitboardBlack;
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardBlack;
        numChecks = GetCheckDefenseMask(&moveContext);
        encodedMoves = GetValidKingCaptures(&moveContext, moveContext.friendlyKingSquare);

        while (encodedMoves != 0)
        {
            moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
            moves[moveCounter].from = moveContext.friendlyKingSquare;
            moves[moveCounter].piece = King;
            moves[moveCounter].promotion = None;
            moveCounter++;
            encodedMoves = intrinsic_blsr64(encodedMoves);
        }
        if (numChecks == 2) // In double check, only the king can move.
            return moveCounter;

        moveCounter += GetAllValidBlackPawnMoves(&moveContext, &moves[moveCounter], true);
    }

    // Check the other pieces.
    moveCounter += GetAllValidNonPawnNonKingCaptures(&moveContext, Knight, &moves[moveCounter]);
    moveCounter += GetAllValidNonPawnNonKingCaptures(&moveContext, Bishop, &moves[moveCounter]);
    moveCounter += GetAllValidNonPawnNonKingCaptures(&moveContext, Rook, &moves[moveCounter]);
    moveCounter += GetAllValidNonPawnNonKingCaptures(&moveContext, Queen, &moves[moveCounter]);

    return moveCounter;
}

// TODO: May need to return a value > 255. Should be easily handled with thread_local move storage in evaluation, though.
uint8_t GetPseudoLegalMoves(const Board * board, Move * moves)
{
    MoveContext moveContext = { 0 };
    EncodedSquare encodedMoves;
    uint8_t moveCounter = 0;

    moveContext.board = board;
    moveContext.player = board->playerToMove;

    if (board->playerToMove == White)
    {
        moveContext.friendlyPieceTables = board->whitePieceTables;
        moveContext.friendlyKingSquare = board->whiteKingSquare;
        moveContext.opponentKingSquare = board->blackKingSquare;
        moveContext.opponentPieceTables = board->blackPieceTables;
        moveContext.friendlyShortPawnMoves = s_pawnShortMoveBitboardWhite;
        moveContext.friendlyLongPawnMoves = s_pawnLongMoveBitboardWhite;
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardWhite;
        encodedMoves = GetWhiteKingPseudoLegalMoves(&moveContext, moveContext.friendlyKingSquare);

        while (encodedMoves != 0)
        {
            moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
            moves[moveCounter].from = moveContext.friendlyKingSquare;
            moves[moveCounter].piece = King;
            moves[moveCounter].promotion = None;
            moveCounter++;
            encodedMoves = intrinsic_blsr64(encodedMoves);
        }

        moveCounter += GetAllPseudoLegalWhitePawnMoves(&moveContext, &moves[moveCounter]);
    }
    else
    {
        moveContext.friendlyPieceTables = board->blackPieceTables;
        moveContext.friendlyKingSquare = board->blackKingSquare;
        moveContext.opponentKingSquare = board->whiteKingSquare;
        moveContext.opponentPieceTables = board->whitePieceTables;
        moveContext.friendlyShortPawnMoves = s_pawnShortMoveBitboardBlack;
        moveContext.friendlyLongPawnMoves = s_pawnLongMoveBitboardBlack;
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardBlack;
        encodedMoves = GetBlackKingPseudoLegalMoves(&moveContext, moveContext.friendlyKingSquare);

        while (encodedMoves != 0)
        {
            moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
            moves[moveCounter].from = moveContext.friendlyKingSquare;
            moves[moveCounter].piece = King;
            moves[moveCounter].promotion = None;
            moveCounter++;
            encodedMoves = intrinsic_blsr64(encodedMoves);
        }

        moveCounter += GetAllPseudoLegalBlackPawnMoves(&moveContext, &moves[moveCounter]);
    }

    // Check the other pieces.
    moveCounter += GetPseudoLegalNonPawnNonKingMoves(&moveContext, Knight, &moves[moveCounter]);
    moveCounter += GetPseudoLegalNonPawnNonKingMoves(&moveContext, Bishop, &moves[moveCounter]);
    moveCounter += GetPseudoLegalNonPawnNonKingMoves(&moveContext, Rook, &moves[moveCounter]);
    moveCounter += GetPseudoLegalNonPawnNonKingMoves(&moveContext, Queen, &moves[moveCounter]);

    return moveCounter;
}

uint8_t GetPseudoLegalCaptures(const Board * board, Move * moves)
{
    MoveContext moveContext = { 0 };
    EncodedSquare encodedMoves;
    Rank promotionRank;
    uint8_t moveCounter = 0;

    moveContext.board = board;
    moveContext.player = board->playerToMove;

    if (board->playerToMove == White)
    {
        moveContext.friendlyPieceTables = board->whitePieceTables;
        moveContext.friendlyKingSquare = board->whiteKingSquare;
        moveContext.opponentKingSquare = board->blackKingSquare;
        moveContext.opponentPieceTables = board->blackPieceTables;
        // Don't need pawn single or double moves; these can never capture.
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardWhite;
        promotionRank = Rank8;
    }
    else
    {
        moveContext.friendlyPieceTables = board->blackPieceTables;
        moveContext.friendlyKingSquare = board->blackKingSquare;
        moveContext.opponentKingSquare = board->whiteKingSquare;
        moveContext.opponentPieceTables = board->whitePieceTables;
        // Don't need pawn single or double moves; these can never capture.
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardBlack;
        promotionRank = Rank1;
    }

    encodedMoves = GetKingPseudoLegalCaptures(&moveContext, moveContext.friendlyKingSquare);

    while (encodedMoves != 0)
    {
        moves[moveCounter].to = SquareDecodeLowest(encodedMoves);
        moves[moveCounter].from = moveContext.friendlyKingSquare;
        moves[moveCounter].piece = King;
        moves[moveCounter].promotion = None;
        moveCounter++;
        encodedMoves = intrinsic_blsr64(encodedMoves);
    }

    // Check the other pieces.
    moveCounter += GetAllPseudoLegalPawnCaptures(&moveContext, &moves[moveCounter], promotionRank);
    moveCounter += GetPseudoLegalNonPawnNonKingCaptures(&moveContext, Knight, &moves[moveCounter]);
    moveCounter += GetPseudoLegalNonPawnNonKingCaptures(&moveContext, Bishop, &moves[moveCounter]);
    moveCounter += GetPseudoLegalNonPawnNonKingCaptures(&moveContext, Rook, &moves[moveCounter]);
    moveCounter += GetPseudoLegalNonPawnNonKingCaptures(&moveContext, Queen, &moves[moveCounter]);

    return moveCounter;
}

bool IsMoveValid(const Board * board, Move move)
{
    MoveContext moveContext = { 0 };
    moveContext.board = board;
    moveContext.player = board->playerToMove;
    if (board->playerToMove == White)
    {
        moveContext.friendlyPieceTables = board->whitePieceTables;
        moveContext.friendlyKingSquare = board->whiteKingSquare;
        moveContext.opponentKingSquare = board->blackKingSquare;
        moveContext.opponentPieceTables = board->blackPieceTables;
        moveContext.friendlyShortPawnMoves = s_pawnShortMoveBitboardWhite;
        moveContext.friendlyLongPawnMoves = s_pawnLongMoveBitboardWhite;
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardWhite;
    }
    else
    {
        moveContext.friendlyPieceTables = board->blackPieceTables;
        moveContext.friendlyKingSquare = board->blackKingSquare;
        moveContext.opponentKingSquare = board->whiteKingSquare;
        moveContext.opponentPieceTables = board->whitePieceTables;
        moveContext.friendlyShortPawnMoves = s_pawnShortMoveBitboardBlack;
        moveContext.friendlyLongPawnMoves = s_pawnLongMoveBitboardBlack;
        moveContext.friendlyPawnAttacks = s_pawnAttackBitboardBlack;
    }

    // Make sure the King is not in check; King can never be taken.
    if (move.piece == King)
    {
        // Prevent moving into check.
        if (KingPseudoLegalSquareIsAttacked(&moveContext, move.to))
            return false;

        // Prevent castling through check. This is a simplified check for castling; normal moves are only one square, so checking
        // for a move of 2+ files must indicate a castle.
        int toFile = SquareGetFile(move.to);
        int fromFile = SquareGetFile(move.from);
        if (toFile == fromFile + 2)
        {
            int toRank = SquareGetRank(move.to);
            assert(toRank == SquareGetRank(move.from));

            if (SquareIsAttacked(&moveContext, SquareFromRankFile(toRank, fromFile)))
                return false;
            if (SquareIsAttacked(&moveContext, SquareFromRankFile(toRank, fromFile + 1)))
                return false;
            // No need to check "to" position; that is covered in if-statement further up.
        }
        else if (toFile == fromFile - 2)
        {
            int toRank = SquareGetRank(move.to);
            assert(toRank == SquareGetRank(move.from));

            if (SquareIsAttacked(&moveContext, SquareFromRankFile(toRank, fromFile)))
                return false;
            if (SquareIsAttacked(&moveContext, SquareFromRankFile(toRank, fromFile - 1)))
                return false;
            // No need to check "to" position; that is covered in if-statement further up.
        }
        // Otherwise, shouldn't be a castling move.
    }
    else
    {
        // If not moving the king, make sure the king is not in check after the move.
        // Simplest way to do this is a quick-and-dirty make move. I think this is probably
        // faster anyway, compared to calculating pinned blockers and attackers.
        // For this operation, we don't need to deal with promotions (position doesn't change)
        // but we do need to handle en passant.
        EncodedSquare fromMask = SquareEncode(move.from);
        EncodedSquare toMask = SquareEncode(move.to);
        EncodedSquare toMaskInverse = ~toMask;
        EncodedSquare fromToMask = fromMask | toMask;
        // TODO: Can probably do this once by lifting it out of this function. Saves some cycles.
        uint64_t allPieceTables = board->allPieceTables;
        uint64_t opponentRooksQueens = moveContext.opponentPieceTables[PIECE_TABLE_ROOKS_QUEENS];
        uint64_t opponentBishopsQueens = moveContext.opponentPieceTables[PIECE_TABLE_BISHOPS_QUEENS];
        uint64_t opponentKnights = moveContext.opponentPieceTables[PIECE_TABLE_KNIGHTS];
        uint64_t opponentPawns = moveContext.opponentPieceTables[PIECE_TABLE_PAWNS];
        uint64_t opponentKing = SquareEncode(moveContext.opponentKingSquare);
        if (move.piece == Pawn && move.to == board->enPassantSquare)
        {
            if (board->playerToMove == White)
            {
                opponentPawns &= ~(toMask >> 8);
                allPieceTables &= ~(toMask >> 8);
            }
            else
            {
                opponentPawns &= ~(toMask << 8);
                allPieceTables &= ~(toMask << 8);
            }
            allPieceTables ^= fromToMask;
        }
        else
        {
            allPieceTables &= ~fromMask;
            allPieceTables |= toMask;
            opponentRooksQueens &= toMaskInverse;
            opponentBishopsQueens &= toMaskInverse;
            opponentKnights &= toMaskInverse;
            opponentPawns &= toMaskInverse;
            opponentKing &= toMaskInverse;
        }

        if ((GetRookMoves(allPieceTables, moveContext.friendlyKingSquare) & opponentRooksQueens) ||
            (GetBishopMoves(allPieceTables, moveContext.friendlyKingSquare) & opponentBishopsQueens) ||
            (GetKnightMoves(moveContext.friendlyKingSquare) & opponentKnights) ||
            (GetPawnCaptureMoves(moveContext.friendlyPawnAttacks, moveContext.friendlyKingSquare) & opponentPawns) ||
            (GetKingMoves(moveContext.friendlyKingSquare) & opponentKing))
        {
            return false;
        }
    }
    return true;
}

// TODO: Maybe some testing to see if it is faster to do the MidGameScalar/EndGameScalar lookups in these functions,
// or to pass in floats and only do the lookups once per MakeMove().
static FORCE_INLINE int32_t GetMovedPieceEval(unsigned long long pieceCountBefore, unsigned long long pieceCountAfter, Player player, PieceType piece, Square from, Square to)
{
    int32_t toScore = GetPieceValue(MidGameScalar[pieceCountAfter], EndGameScalar[pieceCountAfter], player, piece, to);
    int32_t fromScore = GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], player, piece, from);
    return toScore - fromScore;
}

static FORCE_INLINE int32_t GetPromotionPieceEval(unsigned long long pieceCountBefore, unsigned long long pieceCountAfter, Player player, PieceType promotion, Square from, Square to)
{
    return GetPieceValue(MidGameScalar[pieceCountAfter], EndGameScalar[pieceCountAfter], player, promotion, to) -
           GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], player, Pawn, from);
}

#ifdef MAKE_UNMAKE_MOVE
void MakeMove(Board * board, Move move, MakeUnmakeState * state)
#else
void MakeMove(Board * board, Move move)
#endif
{
    EncodedSquare fromMask = SquareEncode(move.from);
    EncodedSquare toMask = SquareEncode(move.to);
    EncodedSquare toMaskInverse = ~toMask;
    EncodedSquare fromToMask = fromMask | toMask;

    // This function requires the board to be unmodified (prior to executing the move)
    // so call this before doing anything else.
    ZobristMerge(board, move);

    uint64_t enPassantSquare = board->enPassantSquare;
    board->enPassantSquare = SquareInvalid; // Default condition.

    board->ply++;

#ifdef MAKE_UNMAKE_MOVE
    state->enPassantSquare = board->enPassantSquare;
    state->halfmoveCounter = board->halfmoveCounter;
    state->capturedPiece = CapturedNone;
    state->castleBits = board->castleBits;
    state->staticEval = board->staticEval;
#endif

    board->halfmoveCounter++; // Increment halfmove counter by default. If this move ends up being a pawn move or capture, it will be reset.

    unsigned long long pieceCountBefore = intrinsic_popcnt64(board->allPieceTables);
    if (pieceCountBefore > 23)
        pieceCountBefore = 23;

    if (board->playerToMove == White)
    {
        unsigned long long pieceCountAfter = pieceCountBefore;
        if (board->blackPieceTables[PIECE_TABLE_COMBINED] & toMask)
            pieceCountAfter--;

        switch (move.piece)
        {
        case Pawn:
        {
            Rank toRank = SquareGetRank(move.to);
            board->whitePieceTables[PIECE_TABLE_PAWNS] ^= fromMask;
            switch (move.promotion)
            {
            case Knight:
                board->whitePieceTables[PIECE_TABLE_KNIGHTS] |= toMask;
                board->staticEval += GetPromotionPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.promotion, move.from, move.to);
                break;
            case Bishop:
                board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= toMask;
                board->staticEval += GetPromotionPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.promotion, move.from, move.to);
                break;
            case Rook:
                board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] |= toMask;
                board->staticEval += GetPromotionPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.promotion, move.from, move.to);
                break;
            case Queen:
                board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= toMask;
                board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] |= toMask;
                board->staticEval += GetPromotionPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.promotion, move.from, move.to);
                break;
            case None:
                // Standard move (not a promotion)
                board->whitePieceTables[PIECE_TABLE_PAWNS] |= toMask;

                if (move.to == enPassantSquare)
                {
                    board->blackPieceTables[PIECE_TABLE_PAWNS] &= ~(toMask >> 8);
                    board->blackPieceTables[PIECE_TABLE_COMBINED] &= ~(toMask >> 8);
                    board->staticEval += GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], Black, Pawn, SquareMoveRankDown(move.to));
                    pieceCountAfter = pieceCountBefore - 1;
                }
                else if (toRank == Rank4 && SquareGetRank(move.from) == Rank2)
                    board->enPassantSquare = SquareMoveRankUp(move.from);

                board->staticEval += GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
                break;
            }

            board->halfmoveCounter = 0;
        }
        break;
        case Knight:
            board->whitePieceTables[PIECE_TABLE_KNIGHTS] ^= fromToMask;
            board->staticEval += GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
            break;
        case Bishop:
            board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= fromToMask;
            board->staticEval += GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
            break;
        case Rook:
            if (move.from == SquareA1)
                board->castleBits &= ~WhiteLongCastle;
            else if (move.from == SquareH1)
                board->castleBits &= ~WhiteShortCastle;

            board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= fromToMask;
            board->staticEval += GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
            break;
        case Queen:
            board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= fromToMask;
            board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= fromToMask;
            board->staticEval += GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
            break;
        case King:
            board->whiteKingSquare = move.to;

            if (move.from == SquareE1)
            {
                if (move.to == SquareG1)
                {
                    board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= 0x00000000000000A0;
                    board->whitePieceTables[PIECE_TABLE_COMBINED] ^= 0x00000000000000A0;
                    board->staticEval += GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, Rook, SquareH1, SquareF1);
                }
                else if (move.to == SquareC1)
                {
                    board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= 0x0000000000000009;
                    board->whitePieceTables[PIECE_TABLE_COMBINED] ^= 0x0000000000000009;
                    board->staticEval += GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, Rook, SquareA1, SquareD1);
                }
            }

            board->staticEval += GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
            board->castleBits &= ~WhiteCastle;
            break;
        }

        // Handle capture of unmoved rooks; prevents castling.
        if (move.to == SquareA8)
            board->castleBits &= ~BlackLongCastle;
        else if (move.to == SquareH8)
            board->castleBits &= ~BlackShortCastle;

        board->whitePieceTables[PIECE_TABLE_COMBINED] ^= fromToMask;
        board->blackPieceTables[PIECE_TABLE_COMBINED] &= toMaskInverse;

        // When updating the combined table, also check for captures and reset the halfmove counter accordingly.
        if (pieceCountAfter < pieceCountBefore)
        {
            assert(pieceCountAfter == pieceCountBefore - 1);

            // Capture occured; reset the halfmove counter.
            board->halfmoveCounter = 0;

#ifdef MAKE_UNMAKE_MOVE
            state->capturedPiece |= (board->blackPieceTables[PIECE_TABLE_PAWNS] & toMask) >> move->to;
            state->capturedPiece |= ((board->blackPieceTables[PIECE_TABLE_KNIGHTS] & toMask) >> move->to) << 1;
            state->capturedPiece |= ((board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] & toMask) >> move->to) << 2;
            state->capturedPiece |= ((board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] & toMask) >> move->to) << 3;
#endif

            // Update piece tables and static eval.
            if (board->blackPieceTables[PIECE_TABLE_PAWNS] & toMask)
            {
                board->blackPieceTables[PIECE_TABLE_PAWNS] &= toMaskInverse;
                board->staticEval += GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], Black, Pawn, move.to);
            }
            else if (board->blackPieceTables[PIECE_TABLE_KNIGHTS] & toMask)
            {
                board->blackPieceTables[PIECE_TABLE_KNIGHTS] &= toMaskInverse;
                board->staticEval += GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], Black, Knight, move.to);
            }
            else if (board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] & toMask)
            {
                board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] &= toMaskInverse;
                if (board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] & toMask)
                {
                    // Queen
                    board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] &= toMaskInverse;
                    board->staticEval += GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], Black, Queen, move.to);
                }
                else
                {
                    // Bishop
                    board->staticEval += GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], Black, Bishop, move.to);
                }
            }
            else if (board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] & toMask)
            {
                // Rook
                board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] &= toMaskInverse;
                board->staticEval += GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], Black, Rook, move.to);
            }
#ifdef _DEBUG
            else
            {
                assert(move.to == enPassantSquare);
            }
#endif
        }
    }
    else
    {
        unsigned long long pieceCountAfter = pieceCountBefore;
        if (board->whitePieceTables[PIECE_TABLE_COMBINED] & toMask)
            pieceCountAfter--;

        switch (move.piece)
        {
        case Pawn:
        {
            Rank toRank = SquareGetRank(move.to);
            board->blackPieceTables[PIECE_TABLE_PAWNS] ^= fromMask;
            switch (move.promotion)
            {
            case Knight:
                board->blackPieceTables[PIECE_TABLE_KNIGHTS] |= toMask;
                board->staticEval -= GetPromotionPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.promotion, move.from, move.to);
                break;
            case Bishop:
                board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= toMask;
                board->staticEval -= GetPromotionPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.promotion, move.from, move.to);
                break;
            case Rook:
                board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] |= toMask;
                board->staticEval -= GetPromotionPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.promotion, move.from, move.to);
                break;
            case Queen:
                board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= toMask;
                board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] |= toMask;
                board->staticEval -= GetPromotionPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.promotion, move.from, move.to);
                break;
            case None:
                // Standard move (not a promotion)
                board->blackPieceTables[PIECE_TABLE_PAWNS] |= toMask;

                if (move.to == enPassantSquare)
                {
                    board->whitePieceTables[PIECE_TABLE_PAWNS] &= ~(toMask << 8);
                    board->whitePieceTables[PIECE_TABLE_COMBINED] &= ~(toMask << 8);
                    board->staticEval -= GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], White, Pawn, SquareMoveRankUp(move.to));
                    pieceCountAfter = pieceCountBefore - 1;
                }
                else if (toRank == Rank5 && SquareGetRank(move.from) == Rank7)
                    board->enPassantSquare = SquareMoveRankDown(move.from);

                board->staticEval -= GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
                break;
            }

            board->halfmoveCounter = 0;
        }
        break;
        case Knight:
            board->blackPieceTables[PIECE_TABLE_KNIGHTS] ^= fromToMask;
            board->staticEval -= GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
            break;
        case Bishop:
            board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= fromToMask;
            board->staticEval -= GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
            break;
        case Rook:
            if (move.from == SquareA8)
                board->castleBits &= ~BlackLongCastle;
            else if (move.from == SquareH8)
                board->castleBits &= ~BlackShortCastle;

            board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= fromToMask;
            board->staticEval -= GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
            break;
        case Queen:
            board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= fromToMask;
            board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= fromToMask;
            board->staticEval -= GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
            break;
        case King:
            board->blackKingSquare = move.to;

            if (move.from == SquareE8)
            {
                if (move.to == SquareG8)
                {
                    board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= 0xA000000000000000;
                    board->blackPieceTables[PIECE_TABLE_COMBINED] ^= 0xA000000000000000;
                    board->staticEval -= GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, Rook, SquareH8, SquareF8);
                }
                else if (move.to == SquareC8)
                {
                    board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= 0x0900000000000000;
                    board->blackPieceTables[PIECE_TABLE_COMBINED] ^= 0x0900000000000000;
                    board->staticEval -= GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, Rook, SquareA8, SquareD8);
                }
            }

            board->staticEval -= GetMovedPieceEval(pieceCountBefore, pieceCountAfter, board->playerToMove, move.piece, move.from, move.to);
            board->castleBits &= ~BlackCastle;
            break;
        }

        // Handle capture of unmoved rooks; prevents castling.
        if (move.to == SquareA1)
            board->castleBits &= ~WhiteLongCastle;
        else if (move.to == SquareH1)
            board->castleBits &= ~WhiteShortCastle;

        board->blackPieceTables[PIECE_TABLE_COMBINED] ^= fromToMask;
        board->whitePieceTables[PIECE_TABLE_COMBINED] &= toMaskInverse;

        // When updating the combined table, also check for captures and reset the halfmove counter accordingly.
        if (pieceCountAfter < pieceCountBefore)
        {
            assert(pieceCountAfter == pieceCountBefore - 1);

            // Capture occured; reset the halfmove counter.
            board->halfmoveCounter = 0;

#ifdef MAKE_UNMAKE_MOVE
            state->capturedPiece |= (board->whitePieceTables[PIECE_TABLE_PAWNS] & toMask) >> move->to;
            state->capturedPiece |= ((board->whitePieceTables[PIECE_TABLE_KNIGHTS] & toMask) >> move->to) << 1;
            state->capturedPiece |= ((board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] & toMask) >> move->to) << 2;
            state->capturedPiece |= ((board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] & toMask) >> move->to) << 3;
#endif
            // Update piece tables and static eval.
            if (board->whitePieceTables[PIECE_TABLE_PAWNS] & toMask)
            {
                board->whitePieceTables[PIECE_TABLE_PAWNS] &= toMaskInverse;
                board->staticEval -= GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], White, Pawn, move.to);
            }
            else if (board->whitePieceTables[PIECE_TABLE_KNIGHTS] & toMask)
            {
                board->whitePieceTables[PIECE_TABLE_KNIGHTS] &= toMaskInverse;
                board->staticEval -= GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], White, Knight, move.to);
            }
            else if (board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] & toMask)
            {
                board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] &= toMaskInverse;
                if (board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] & toMask)
                {
                    // Queen
                    board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] &= toMaskInverse;
                    board->staticEval -= GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], White, Queen, move.to);
                }
                else
                {
                    // Bishop
                    board->staticEval -= GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], White, Bishop, move.to);
                }
            }
            else if (board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] & toMask)
            {
                // Rook
                board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] &= toMaskInverse;
                board->staticEval -= GetPieceValue(MidGameScalar[pieceCountBefore], EndGameScalar[pieceCountBefore], White, Rook, move.to);
            }
#ifdef _DEBUG
            else
            {
                assert(move.to == enPassantSquare);
            }
#endif
        }
    }

    board->allPieceTables = board->whitePieceTables[PIECE_TABLE_COMBINED] | board->blackPieceTables[PIECE_TABLE_COMBINED];

    board->playerToMove = !board->playerToMove;
}

#ifdef MAKE_UNMAKE_MOVE
bool MakeMove2(Board * board, Move move, Player * player, MakeUnmakeState * state)
#else
bool MakeMove2(Board * board, Move move, Player * player)
#endif
{
    PieceType pieceType;
    BoardGetPlayerPieceAtSquare(board, move.from, &pieceType, player);
    if (pieceType == None || (*player != board->playerToMove))
        return false;
    move.piece = pieceType;
#ifdef MAKE_UNMAKE_MOVE
    MakeMove(board, move, state);
#else
    MakeMove(board, move);
#endif
    return true;
}

#ifdef MAKE_UNMAKE_MOVE
#error MakeNullMove not yet implemented!
#else
extern void MakeNullMove(Board * board)
{
    // Switch which player can move next, and manually increase the ply.
    ZobristSwapPlayer(board);
    board->ply++;
    board->enPassantSquare = SquareInvalid;
    board->halfmoveCounter++;
    board->playerToMove = !board->playerToMove;
}
#endif

#ifdef MAKE_UNMAKE_MOVE
void UnmakeMove(Board * board, Player player, Move move, const MakeUnmakeState * state)
{
    EncodedSquare fromMask = SquareEncode(move->from);
    EncodedSquare toMask = SquareEncode(move->to);
    EncodedSquare fromToMask = fromMask | toMask;

    if (player == White)
    {
        switch (move.piece)
        {
        case Pawn:
        {
            if (SquareGetRank(move.to) == Rank8)
            {
                board->whitePieceTables[PIECE_TABLE_PAWNS] |= fromMask;

                switch (move.promotion)
                {
                case Knight:
                    board->whitePieceTables[PIECE_TABLE_KNIGHTS] ^= toMask;
                    break;
                case Bishop:
                    board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= toMask;
                    break;
                case Rook:
                    board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= toMask;
                    break;
                case Queen:
                    board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= toMask;
                    board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= toMask;
                    break;
                }
            }
            else
            {
                board->whitePieceTables[PIECE_TABLE_PAWNS] ^= fromToMask;

                if (move.to == state->enPassantSquare)
                {
                    board->blackPieceTables[PIECE_TABLE_PAWNS] |= (toMask >> 8);
                    board->blackPieceTables[PIECE_TABLE_COMBINED] |= (toMask >> 8);
                }
            }
        }
        break;
        case Knight:
            board->whitePieceTables[PIECE_TABLE_KNIGHTS] ^= fromToMask;
            break;
        case Bishop:
            board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= fromToMask;
            break;
        case Rook:
            board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= fromToMask;
            break;
        case Queen:
            board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= fromToMask;
            board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= fromToMask;
            break;
        case King:
            board->whiteKingSquare = move.from;

            if (move.from == SquareE1)
            {
                if (move.to == SquareG1)
                {
                    board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= 0x00000000000000A0;
                    board->whitePieceTables[PIECE_TABLE_COMBINED] ^= 0x00000000000000A0;
                }
                else if (move.to == SquareC1)
                {
                    board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= 0x0000000000000009;
                    board->whitePieceTables[PIECE_TABLE_COMBINED] ^= 0x0000000000000009;
                }
            }
            break;
        }

        board->whitePieceTables[PIECE_TABLE_COMBINED] ^= fromToMask;

        switch (state->capturedPiece)
        {
        case CapturedPawn:
            board->blackPieceTables[PIECE_TABLE_PAWNS] |= toMask;
            board->blackPieceTables[PIECE_TABLE_COMBINED] |= toMask;
            break;
        case CapturedKnight:
            board->blackPieceTables[PIECE_TABLE_KNIGHTS] |= toMask;
            board->blackPieceTables[PIECE_TABLE_COMBINED] |= toMask;
            break;
        case CapturedBishop:
            board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= toMask;
            board->blackPieceTables[PIECE_TABLE_COMBINED] |= toMask;
            break;
        case CapturedRook:
            board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] |= toMask;
            board->blackPieceTables[PIECE_TABLE_COMBINED] |= toMask;
            break;
        case CapturedQueen:
            board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= toMask;
            board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] |= toMask;
            board->blackPieceTables[PIECE_TABLE_COMBINED] |= toMask;
            break;
        }
    }
    else
    {
        switch (move.piece)
        {
        case Pawn:
        {
            if (SquareGetRank(move.to) == Rank1)
            {
                board->blackPieceTables[PIECE_TABLE_PAWNS] |= fromMask;
                switch (move.promotion)
                {
                case Knight:
                    board->blackPieceTables[PIECE_TABLE_KNIGHTS] ^= toMask;
                    break;
                case Bishop:
                    board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= toMask;
                    break;
                case Rook:
                    board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= toMask;
                    break;
                case Queen:
                    board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= toMask;
                    board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= toMask;
                    break;
                }
            }
            else
            {
                board->blackPieceTables[PIECE_TABLE_PAWNS] ^= fromToMask;

                if (move.to == state->enPassantSquare)
                {
                    board->whitePieceTables[PIECE_TABLE_PAWNS] |= (toMask >> 8);
                    board->whitePieceTables[PIECE_TABLE_COMBINED] |= (toMask >> 8);
                }
            }
        }
        break;
        case Knight:
            board->blackPieceTables[PIECE_TABLE_KNIGHTS] ^= fromToMask;
            break;
        case Bishop:
            board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= fromToMask;
            break;
        case Rook:
            board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= fromToMask;
            break;
        case Queen:
            board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] ^= fromToMask;
            board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= fromToMask;
            break;
        case King:
            board->blackKingSquare = move.from;

            if (move.from == SquareE8)
            {
                if (move.to == SquareG8)
                {
                    board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= 0xA000000000000000;
                    board->blackPieceTables[PIECE_TABLE_COMBINED] ^= 0xA000000000000000;
                }
                else if (move.to == SquareC8)
                {
                    board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] ^= 0x0900000000000000;
                    board->blackPieceTables[PIECE_TABLE_COMBINED] ^= 0x0900000000000000;
                }
            }
            break;
        }

        board->blackPieceTables[PIECE_TABLE_COMBINED] ^= fromToMask;

        switch (state->capturedPiece)
        {
        case CapturedPawn:
            board->whitePieceTables[PIECE_TABLE_PAWNS] |= toMask;
            board->whitePieceTables[PIECE_TABLE_COMBINED] |= toMask;
            break;
        case CapturedKnight:
            board->whitePieceTables[PIECE_TABLE_KNIGHTS] |= toMask;
            board->whitePieceTables[PIECE_TABLE_COMBINED] |= toMask;
            break;
        case CapturedBishop:
            board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= toMask;
            board->whitePieceTables[PIECE_TABLE_COMBINED] |= toMask;
            break;
        case CapturedRook:
            board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] |= toMask;
            board->whitePieceTables[PIECE_TABLE_COMBINED] |= toMask;
            break;
        case CapturedQueen:
            board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= toMask;
            board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] |= toMask;
            board->whitePieceTables[PIECE_TABLE_COMBINED] |= toMask;
            break;
        }
    }

    board->ply--;
    board->halfmoveCounter = state->halfmoveCounter;
    board->enPassantSquare = state->enPassantSquare;
    board->castleBits = state->castleBits;
    board->staticEval = state->staticEval;
    board->allPieceTables = board->whitePieceTables[PIECE_TABLE_COMBINED] | board->blackPieceTables[PIECE_TABLE_COMBINED];
    board->playerToMove = !board->playerToMove;

    // TODO: Should be able to optimize this with a reverse-merge function.
    board->hash = ZobristCalculate(board);
    board->materialHash = ZobristCalculateMaterialHash(board);
}
#endif
