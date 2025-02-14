#ifndef BOARD_H_
#define BOARD_H_

#include "PieceType.h"
#include "Player.h"
#include "Square.h"
#include "StaticAssert.h"

#include <stdint.h>
#include <string.h>

#define NUM_SQUARES 64

#define PIECE_TABLE_COMBINED        0
#define PIECE_TABLE_PAWNS           1
#define PIECE_TABLE_KNIGHTS         2
#define PIECE_TABLE_BISHOPS_QUEENS  3
#define PIECE_TABLE_ROOKS_QUEENS    4
#define NUM_PIECE_TABLES            5

typedef enum
{
    NoCastle         = 0x00,

    WhiteShortCastle = 0x01,
    WhiteLongCastle  = 0x02,
    BlackShortCastle = 0x04,
    BlackLongCastle  = 0x08,

    WhiteCastle = WhiteShortCastle | WhiteLongCastle,
    BlackCastle = BlackShortCastle | BlackLongCastle,
    AllCastle   = WhiteCastle | BlackCastle

} CastleBits;

// We try to keep this struct <= 128 bytes so it can fit in two cache lines.
//
// TODO: REPLACE KING PIECE TABLES WITH SQUARES!
// This will save 14 bytes at the expense of some additional bit shifts. This is almost definitely worth it so we can store material hash, player data, etc.
//
//
typedef struct
{
    // Add one table for all pieces combined. This combined table uses index 0 so that
    // the PieceType enum can index directly into the array.
    // Everything is kept in arrays like this instead of being broken out to allow the compiler
    // to pack the struct nicely and maybe take advantage of some caching benefits.
    uint64_t whitePieceTables[NUM_PIECE_TABLES];
    uint64_t blackPieceTables[NUM_PIECE_TABLES];
    uint64_t allPieceTables;
    uint64_t hash; // Zobrist hash
    uint64_t materialHash; // Simplified material hash
    int32_t staticEval; // TODO: Remove this once we get some things settled.
    uint16_t ply; // Number of half-moves
    Square whiteKingSquare;
    Square blackKingSquare;
    Square enPassantSquare;
    Player playerToMove;
    // TODO: Since we are storing the hash here, which includes Player data, we should also include the Player here as well,
    // rather than hauling around an addition parameter to a bunch of functions.
    uint8_t halfmoveCounter; // Counter to track fifty rule move
    uint8_t castleBits;
} Board;

STATIC_ASSERT(sizeof(Board) <= 128, "sizeof(Board) exceeds two cache lines");

extern void BoardInitializeStartingPosition(Board * board);

extern PieceType BoardGetPieceAtSquare(const Board * board, Square square);
extern void BoardGetPlayerPieceAtSquare(const Board * board, Square square, PieceType * type, Player * player);
extern uint64_t BoardGetPieceTable(const Board * board, Player player, PieceType type);
extern uint8_t BoardGetNumPieces(const Board * board, Player player, PieceType type);

static inline void BoardInitializeEmpty(Board * board)
{
    memset(board, 0, sizeof(Board));
}

#endif // BOARD_H_
