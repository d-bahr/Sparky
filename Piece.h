#ifndef PIECE_H_
#define PIECE_H_

#include "PieceType.h"
#include "Player.h"

typedef struct
{
    PieceType type;
    Player player;

} Piece;

#define PIECE_EQUALS(a, b) (((a).type == (b).type) && ((a).player == (b).player))

#endif // PIECE_H_
