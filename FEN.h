#ifndef FEN_H_
#define FEN_H_

#include "Board.h"
#include "File.h"
#include "Move.h"
#include "Player.h"
#include "Rank.h"
#include "Square.h"

#include <stdbool.h>
#include <stddef.h>

extern bool ParseRank(char c, Rank * r);
extern bool ParseFile(char c, File * f);
extern bool ParsePieceType(char c, PieceType * type);
extern bool ParseSquare(const char * str, Square * square);
extern bool ParseSquareAdvance(const char ** str, Square * square);
extern bool ParseMove(const char * str, Move * move);
extern size_t MoveToString(Move move, char * str, size_t len);
extern bool ParseIntegerFromString(const char ** str, uint32_t * value);
extern bool ParseFEN(const char * fen, Board * board);
extern size_t ToFEN(const Board * board, char * str, size_t len);

#endif // FEN_H_
