#ifndef MOVE_GENERATION_H_
#define MOVE_GENERATION_H_

#include "Board.h"
#include "Intrinsics.h"
#include "Move.h"
#include "Player.h"

#include "tables/MoveTables.h"

#include <stdint.h>

static FORCE_INLINE int32_t NumPawns(const uint64_t * pieceTables)
{
    return (int32_t) intrinsic_popcnt64(pieceTables[PIECE_TABLE_PAWNS]);
}

static FORCE_INLINE int32_t NumKnights(const uint64_t * pieceTables)
{
    return (int32_t) intrinsic_popcnt64(pieceTables[PIECE_TABLE_KNIGHTS]);
}

static FORCE_INLINE int32_t NumBishops(const uint64_t * pieceTables)
{
    return (int32_t) intrinsic_popcnt64(intrinsic_andn64(pieceTables[PIECE_TABLE_ROOKS_QUEENS], pieceTables[PIECE_TABLE_BISHOPS_QUEENS]));
}

static FORCE_INLINE int32_t NumRooks(const uint64_t * pieceTables)
{
    return (int32_t) intrinsic_popcnt64(intrinsic_andn64(pieceTables[PIECE_TABLE_BISHOPS_QUEENS], pieceTables[PIECE_TABLE_ROOKS_QUEENS]));
}

static FORCE_INLINE int32_t NumQueens(const uint64_t * pieceTables)
{
    return (int32_t) intrinsic_popcnt64(pieceTables[PIECE_TABLE_BISHOPS_QUEENS] & pieceTables[PIECE_TABLE_ROOKS_QUEENS]);
}

static FORCE_INLINE int32_t NumNonKingPieces(const uint64_t * pieceTables)
{
    return (int32_t) (intrinsic_popcnt64(pieceTables[PIECE_TABLE_COMBINED]) - 1);
}

static FORCE_INLINE EncodedSquare GetPawnCaptureMoves(const uint64_t * pawnAttackBitboard, Square square)
{
    return pawnAttackBitboard[square];
}

static FORCE_INLINE EncodedSquare GetKnightMoves(Square square)
{
    return s_knightMoveBitboard[square];
}

static FORCE_INLINE EncodedSquare GetBishopMoves(EncodedSquare occupiedSquares, Square square)
{
    // TODO: Better cache coherency: s_bishopBlockerBitboards[square] and s_bishopBlockerMasks[square] should be grouped in a single struct to avoid cache thrashing.
    // Same for rooks.
    return s_bishopMoveBitboard[square] & s_bishopBlockerBitboards[square][intrinsic_pext64(occupiedSquares, s_bishopBlockerMasks[square])];
}

static FORCE_INLINE EncodedSquare GetRookMoves(EncodedSquare occupiedSquares, Square square)
{
    return s_rookMoveBitboard[square] & s_rookBlockerBitboards[square][intrinsic_pext64(occupiedSquares, s_rookBlockerMasks[square])];
}

static FORCE_INLINE EncodedSquare GetQueenMoves(EncodedSquare occupiedSquares, Square square)
{
    return GetBishopMoves(occupiedSquares, square) | GetRookMoves(occupiedSquares, square);
}

static FORCE_INLINE EncodedSquare GetKingMoves(Square square)
{
    return s_kingMoveBitboard[square];
}

//#define MAKE_UNMAKE_MOVE

#ifdef MAKE_UNMAKE_MOVE
typedef enum
{
    CapturedNone = 0,
    CapturedPawn = 1 << 0,
    CapturedKnight = 1 << 1,
    CapturedBishop = 1 << 2,
    CapturedRook = 1 << 3,
    CapturedQueen = CapturedBishop | CapturedRook
} CapturedPiece;

typedef struct
{
    Square enPassantSquare;
    int32_t staticEval;
    uint8_t halfmoveCounter;
    uint8_t capturedPiece;
    uint8_t castleBits;

} MakeUnmakeState;

extern void MakeMove(Board * board, Move move, MakeUnmakeState * state);
extern bool MakeMove2(Board * board, Move move, Player * player, MakeUnmakeState * state);
extern void UnmakeMove(Board * board, Move move, const MakeUnmakeState * state);
#else
extern void MakeMove(Board * board, Move move);
extern bool MakeMove2(Board * board, Move move, Player * player);
extern void MakeNullMove(Board * board);
#endif

extern bool KingIsAttacked(const Board * board, Player playerOfKing);
extern bool IsCheckmate(const Board * board);
extern bool IsStalemate(const Board * board);
extern bool HasMoves(const Board * board, Player player);
extern uint8_t GetValidMoves(const Board * board, Move * moves);
extern uint8_t GetValidCaptures(const Board * board, Move * moves);
extern uint8_t GetPseudoLegalMoves(const Board * board, Move * moves);
extern uint8_t GetPseudoLegalCaptures(const Board * board, Move * moves);
extern bool IsMoveValid(const Board * board, Move move);

#endif // MOVE_GENERATION_H_
