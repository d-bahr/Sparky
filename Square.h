#ifndef SQUARE_H_
#define SQUARE_H_

#include "File.h"
#include "Intrinsics.h"
#include "Rank.h"

#include <inttypes.h>
#include <stdint.h>

typedef uint8_t Square;

typedef uint64_t EncodedSquare;

enum
{
    SquareA1 = 0,
    SquareB1, // 1
    SquareC1, // 2
    SquareD1, // 3
    SquareE1, // 4
    SquareF1, // 5
    SquareG1, // 6
    SquareH1, // 7
    SquareA2, // 8
    SquareB2, // 9
    SquareC2, // 10
    SquareD2, // 11
    SquareE2, // 12
    SquareF2, // 13
    SquareG2, // 14
    SquareH2, // 15
    SquareA3, // 16
    SquareB3, // 17
    SquareC3, // 18
    SquareD3, // 19
    SquareE3, // 20
    SquareF3, // 21
    SquareG3, // 22
    SquareH3, // 23
    SquareA4, // 24
    SquareB4, // 25
    SquareC4, // 26
    SquareD4, // 27
    SquareE4, // 28
    SquareF4, // 29
    SquareG4, // 30
    SquareH4, // 31
    SquareA5, // 32
    SquareB5, // 33
    SquareC5, // 34
    SquareD5, // 35
    SquareE5, // 36
    SquareF5, // 37
    SquareG5, // 38
    SquareH5, // 39
    SquareA6, // 40
    SquareB6, // 41
    SquareC6, // 42
    SquareD6, // 43
    SquareE6, // 44
    SquareF6, // 45
    SquareG6, // 46
    SquareH6, // 47
    SquareA7, // 48
    SquareB7, // 49
    SquareC7, // 50
    SquareD7, // 51
    SquareE7, // 52
    SquareF7, // 53
    SquareG7, // 54
    SquareH7, // 55
    SquareA8, // 56
    SquareB8, // 57
    SquareC8, // 58
    SquareD8, // 59
    SquareE8, // 60
    SquareF8, // 61
    SquareG8, // 62
    SquareH8, // 63
    SquareInvalid = 0xFF
};

#define SQUARE_VERTICAL_FLIP(x_) ((x_) ^ 56)
#define SQUARE_HORIZONTAL_FLIP(x_) ((x_) ^ 7)

static FORCE_INLINE EncodedSquare SquareEncode(Square square)
{
    return 1ull << square;
}

static FORCE_INLINE EncodedSquare SquareEncodeRankFile(Rank rank, File file)
{
    // Equivalent to: 1 << (rank * 8 + file).
    return 1ull << (uint64_t) ((rank << 3ull) + file);
}

static FORCE_INLINE Square SquareDecode(EncodedSquare pos)
{
    return (Square) intrinsic_bsr64(pos);
}

static FORCE_INLINE Square SquareDecodeLowest(EncodedSquare pos)
{
    return (Square) intrinsic_bsf64(pos);
}

static FORCE_INLINE Rank SquareGetRank(Square square)
{
    // Equivalent to: square / 8.
    return square >> 3;
}

static FORCE_INLINE File SquareGetFile(Square square)
{
    // Equivalent to: square % 8.
    return square & 0x7;
}

static FORCE_INLINE Square SquareMoveFileLeft(Square square)
{
    return square - 1;
}

static FORCE_INLINE Square SquareMoveFileRight(Square square)
{
    return square + 1;
}

static FORCE_INLINE Square SquareMoveRankUp(Square square)
{
    return square + 8;
}

static FORCE_INLINE Square SquareMoveRankDown(Square square)
{
    return square - 8;
}

// TODO: Compare static inline against #defines.
static FORCE_INLINE Square SquareFromRankFile(Rank rank, File file)
{
    // Equivalent to: (rank * 8) + file.
    return (Square)((rank << 3) + file);
}

#endif // SQUARE_H_
