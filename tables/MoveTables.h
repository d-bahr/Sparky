#ifndef MOVE_TABLES_INC_
#define MOVE_TABLES_INC_

#include "Board.h"

#include <stdint.h>

extern const uint64_t * s_rookBlockerBitboards[NUM_SQUARES];
extern uint64_t * s_rookBlockerBitboardsContiguous;
extern uint64_t s_rookBlockerMasks[NUM_SQUARES];
extern const uint64_t * s_bishopBlockerBitboards[NUM_SQUARES];
extern uint64_t * s_bishopBlockerBitboardsContiguous;
extern uint64_t s_bishopBlockerMasks[NUM_SQUARES];
extern const uint64_t s_pawnAttackBitboardWhite[NUM_SQUARES];
extern const uint64_t s_pawnAttackBitboardBlack[NUM_SQUARES];
extern const uint64_t s_pawnShortMoveBitboardWhite[NUM_SQUARES];
extern const uint64_t s_pawnLongMoveBitboardWhite[NUM_SQUARES];
extern const uint64_t s_pawnShortMoveBitboardBlack[NUM_SQUARES];
extern const uint64_t s_pawnLongMoveBitboardBlack[NUM_SQUARES];
extern const uint64_t s_knightMoveBitboard[NUM_SQUARES];
extern const uint64_t s_bishopMoveBitboard[NUM_SQUARES];
extern const uint64_t s_rookMoveBitboard[NUM_SQUARES];
extern const uint64_t s_queenMoveBitboard[NUM_SQUARES];
extern const uint64_t s_kingMoveBitboard[NUM_SQUARES];

#endif // MOVE_TABLES_INC_
