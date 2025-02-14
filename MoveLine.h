#ifndef MOVE_LINE_H_
#define MOVE_LINE_H_

#include "Intrinsics.h"
#include "Move.h"

#define MAX_LINE_DEPTH 64

typedef struct
{
    int length;
    Move moves[MAX_LINE_DEPTH];
} MoveLine;

static FORCE_INLINE void MoveLineInit(MoveLine * line)
{
    // No real need to initialize moves; can just set length to zero.
    line->length = 0;
}

#endif // MOVE_LINE_H_
