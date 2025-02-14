#ifndef MOVE_ORDERER_H_
#define MOVE_ORDERER_H_

#include "Board.h"
#include "KillerMove.h"
#include "MoveLine.h"
#include "Player.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    Move * moves;
    int32_t * approxScores;
    uint8_t numMoves;
    uint8_t curIndex;
} MoveOrderer;

extern void MoveOrdererInitialize(MoveOrderer * moveOrderer, const Board * board, Move * moves, int32_t * approxScores, uint8_t numMoves, int32_t linePly, const MoveLine * bestLinePrev, const KillerMoves * killerMoves, Move ttMove);
extern bool MoveOrdererGetNextMove(MoveOrderer * moveOrderer, Move * move);
extern void MoveOrdererPrint(const MoveOrderer * moveOrderer);

#endif // MOVE_ORDERER_H_
