#ifndef EVALUATION_H_
#define EVALUATION_H_

#include "Board.h"
#include "MoveLine.h"
#include "Player.h"

#include <stdbool.h>
#include <stdint.h>

#define EVAL_PSEUDO_LEGAL 1

// Agnostic checkmate value; used only for minimax where we
// care about absolute value. This value must be storable in
// the transposition table without loss of accuracy.
#define EVAL_MAX 0x1FFFFF

#define EVAL_CHECKMATE (EVAL_MAX - 20000)

#define EVAL_MAX_CHECKMATE_PLY 10000

#define CHECKMATE_WIN (EVAL_CHECKMATE + EVAL_MAX_CHECKMATE_PLY)

#define CHECKMATE_LOSE (-CHECKMATE_WIN)

// White delivered checkmate and won.
#define CHECKMATE_WHITE (EVAL_CHECKMATE + EVAL_MAX_CHECKMATE_PLY)

// Black delivered checkmate and won.
#define CHECKMATE_BLACK (-CHECKMATE_WHITE)

extern bool EvalStart(const Board * board, uint32_t maxTime, uint32_t maxDepth, MoveLine * bestLine);
extern void EvalStop();
extern bool EvalInit(size_t numTTBuckets);
extern void EvalClear();
extern void EvalDestroy();

#endif // EVALUATION_H_
