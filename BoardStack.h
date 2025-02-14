#ifndef BOARD_STACK_H_
#define BOARD_STACK_H_

#include "Board.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#include <intrin.h>
#elif defined(__GNUC__)
#include <x86intrin.h>
#endif

// Stack of board positions and other required state.
// The depth of searches is pretty low (< 100) so the state is stored in an array
// and we don't bother reclaiming unused memory, since the total unused memory
// is around a 20Kb which is nothing compared to the search tree.
typedef struct
{
    Board * boards;
    uint64_t length;
    uint64_t maxLength;
} BoardStack;

static inline void BoardStackInitialize(BoardStack * stack)
{
    // Initialize with 64 boards. It's very unlikely that we'll ever go beyond that; a search depth of 64 is truly huge.
    stack->maxLength = 64;
    stack->length = 0;
    stack->boards = (Board *)malloc(stack->maxLength * sizeof(Board));
}

static inline void BoardStackDestroy(BoardStack * stack)
{
    free(stack->boards);
    stack->length = 0;
}

static inline bool BoardStackTryExpand(BoardStack * stack)
{
    if (stack->length == stack->maxLength)
    {
        // Expand current board array.
        uint64_t newMaxLength = stack->maxLength * 2;
        Board * newBoards = (Board *) malloc(newMaxLength * sizeof(Board));
        if (!newBoards)
            return false;
        memcpy(newBoards, stack->boards, sizeof(Board) * stack->length);
        free(stack->boards);
        stack->boards = newBoards;
    }

    return true;
}

static inline bool BoardStackPushCopy(BoardStack * stack, const Board * board)
{
    if (!BoardStackTryExpand(stack))
        return false;

    stack->boards[stack->length] = *board;
    stack->length++;
    return true;
}

static inline Board * BoardStackPushNew(BoardStack * stack)
{
    if (!BoardStackTryExpand(stack))
        return 0;

    Board * b = &stack->boards[stack->length];
    BoardInitializeEmpty(b);
    stack->length++;
    return b;
}

static inline Board * BoardStackPushNewCopy(BoardStack * stack)
{
    if (stack->length == 0)
        return 0;

    if (!BoardStackTryExpand(stack))
        return 0;

    Board * b = &stack->boards[stack->length];
    *b = stack->boards[stack->length - 1];
    stack->length++;
    return b;
}

static inline void BoardStackPop(BoardStack * stack)
{
    if (stack->length > 0)
        stack->length--;
}

static inline void BoardStackPopN(BoardStack * stack, uint64_t n)
{
    if (stack->length >= n)
        stack->length -= n;
    else
        stack->length = 0;
}

static inline void BoardStackPopTo(BoardStack * stack, uint64_t newLength)
{
    if (stack->length > newLength)
        stack->length = newLength;
}

static inline Board * BoardStackTop(BoardStack * stack)
{
    return stack->length > 0 ? &stack->boards[stack->length - 1] : 0;
}

static inline Board * BoardStackBottom(BoardStack * stack)
{
    return stack->length > 0 ? &stack->boards[0] : 0;
}

#endif // BOARD_STACK_H_
