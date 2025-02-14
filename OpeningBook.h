#ifndef OPENING_BOOK_H_
#define OPENING_BOOK_H_

#include <Board.h>
#include <Move.h>

#include <stdbool.h>
#include <stdint.h>

extern bool OpeningBookFind(const Board * board, Move * move);

#endif // OPENING_BOOK_H_
