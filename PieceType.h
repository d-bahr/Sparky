#ifndef PIECE_TYPE_H_
#define PIECE_TYPE_H_

#include <stdint.h>

enum
{
    None,
    Pawn,
    Knight,
    Bishop,
    Rook,
    Queen,
    King
};

typedef uint8_t PieceType;

#define NUM_PIECE_TYPES 6

#endif // PIECE_TYPE_H_
