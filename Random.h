#ifndef RANDOM_H_
#define RANDOM_H_

#include <stdint.h>

// Initial RNG seed. Borrowed from Stockfish.
static uint64_t s_randState = 1070372;

static inline uint64_t RandomU64()
{
    // XOR Shift 64 Star.
    // Implementation borrowed from Stockfish.
    s_randState ^= s_randState >> 12, s_randState ^= s_randState << 25, s_randState ^= s_randState >> 27;
    return s_randState * 2685821657736338717LL;
}

static inline void RandomSeed(uint64_t s)
{
    s_randState = s;
}

#endif // RANDOM_H_
