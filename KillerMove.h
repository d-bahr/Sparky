#ifndef KILLER_MOVE_H_
#define KILLER_MOVE_H_

#include "Move.h"

#include <stdint.h>

#define MAX_KILLER_MOVES_PER_PLY 2

typedef struct
{
    Move moves[MAX_KILLER_MOVES_PER_PLY];
    int32_t length;
} KillerMoves;

static FORCE_INLINE void KillerMoveInitialize(KillerMoves * killers)
{
    killers->length = 0;
}

static inline void KillerMoveAdd(KillerMoves * killers, Move move)
{
    if (killers->length >= MAX_KILLER_MOVES_PER_PLY)
    {
        memmove(&killers->moves[1], &killers->moves[0], (MAX_KILLER_MOVES_PER_PLY - 1) * sizeof(Move));
        killers->moves[0] = move;
    }
    else
    {
        memmove(&killers->moves[1], &killers->moves[0], killers->length * sizeof(Move));
        killers->moves[0] = move;
        killers->length++;
    }
}

static inline bool KillerMoveFind(const KillerMoves * killers, Move move)
{
    for (int32_t i = 0; i < killers->length; ++i)
    {
        if (MoveEquals(killers->moves[i], move))
            return true;
    }
    return false;
}

#endif
