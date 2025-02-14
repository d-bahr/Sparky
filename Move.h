#ifndef MOVE_H_
#define MOVE_H_

#include "Piece.h"
#include "Square.h"
#include "StaticAssert.h"

#include <stdbool.h>

// This struct is typically faster because each field is byte-aligned.
typedef struct
{
    Square from;
    Square to;
    uint8_t piece;
    uint8_t promotion;
} Move;

// This struct is tightly packed; useful for things like transposition tables.
// [0:5]   : Square from
// [6:11]  : Square to
// [12:14] : Promotion
// Note that piece information is lost and must be obtained from the board state.
typedef uint16_t EncodedMove;

static FORCE_INLINE EncodedMove MoveEncode(Move move)
{
    return move.from | (((uint16_t)move.to) << 6) | (((uint16_t)move.promotion) << 12);
}

static FORCE_INLINE bool EncodedMoveValid(EncodedMove move)
{
    return move != 0;
}

static inline Move MoveDecode(EncodedMove move)
{
    Move m = { 0 };
    m.from = move & 0x3F;
    m.to = (move >> 6) & 0x3F;
    m.promotion = (move >> 12) & 0x7;
    m.piece = None;
    return m;
}

static FORCE_INLINE bool MoveEquals(Move a, Move b)
{
    // Comparing piece type is unnecessary here, because no pieces can overlap in position. Position test is sufficient.
    return (a.from == b.from) && (a.to == b.to) && (a.promotion == b.promotion);
}

#endif // MOVE_H_
