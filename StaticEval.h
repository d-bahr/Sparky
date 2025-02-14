#ifndef STATIC_EVAL_H_
#define STATIC_EVAL_H_

#include "Board.h"
#include "Intrinsics.h"
#include "PieceType.h"
#include "Square.h"

extern const int32_t PieceValuesMillipawnsMidGame[NUM_PIECE_TYPES + 1];
extern const int32_t PieceValuesMillipawnsEndGame[NUM_PIECE_TYPES + 1];
extern const float MidGameScalar[24];
extern const float EndGameScalar[24];
extern int32_t g_midGameTables[2][NUM_PIECE_TYPES + 1][NUM_SQUARES];
extern int32_t g_endGameTables[2][NUM_PIECE_TYPES + 1][NUM_SQUARES];

static FORCE_INLINE int32_t GetPieceValue(float midGameProgressionScalar, float endGameProgressionScalar, Player player, PieceType piece, Square square)
{
    return (int32_t) ((g_midGameTables[player][piece][square] * midGameProgressionScalar) + (g_endGameTables[player][piece][square] * endGameProgressionScalar));
}

extern int32_t Evaluate(const Board * board);
extern void StaticEvalInitialize();

#endif // STATIC_EVAL_H_
