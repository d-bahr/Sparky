#ifndef ZOBRIST_H_
#define ZOBRIST_H_

#include "Board.h"
#include "Move.h"
#include "Player.h"

#include <stdint.h>

extern void ZobristGenerate();
extern uint64_t ZobristCalculate(const Board * board);
// Gets a hash of only the material, without including hash for castling, en passant, and player to move.
// Requires the board to already contain a valid hash for its current state.
extern uint64_t ZobristCalculateMaterialHash(const Board * board);
extern void ZobristMerge(Board * board, Move move);
extern void ZobristSwapPlayer(Board * board);

#endif // ZOBRIST_H_
