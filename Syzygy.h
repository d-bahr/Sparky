#ifndef SYZYGY_H
#define SYZYGY_H

#include "Board.h"
#include "Move.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum WDLScore
{
    WDLLoss        = -2, // Loss
    WDLBlessedLoss = -1, // Loss, but draw under 50-move rule
    WDLDraw        =  0, // Draw
    WDLCursedWin   =  1, // Win, but draw under 50-move rule
    WDLWin         =  2, // Win
} WDLScore;

// Possible states after a probing operation
typedef enum ProbeState
{
    PROBE_STATE_FAIL              =  0, // Probe failed (missing file table)
    PROBE_STATE_OK                =  1, // Probe successful
    PROBE_STATE_CHANGE_STM        = -1, // DTZ should check the other side
    PROBE_STATE_ZEROING_BEST_MOVE =  2  // Best move zeroes DTZ (capture or pawn move)
} ProbeState;

extern int MaxCardinality;

extern bool SyzygyInit(const char * paths);
extern void SyzygyDestroy();
extern WDLScore SyzygyProbeWDL(const Board * board, ProbeState * result);
extern int SyzygyProbeDTZ(const Board * board, ProbeState * result);
// TODO: Finish implementation. Currently this is disabled because there's no storage of TB probe information per root move.
#if 0
extern bool SyzygyRootProbe(const Board * board, const Move * rootMoves, uint8_t numMoves);
extern bool SyzygyRootProbeWDL(const Board * board, const Move * rootMoves, uint8_t numMoves);
#endif

#endif // SYZYGY_H
