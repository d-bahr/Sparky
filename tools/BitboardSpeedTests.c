#include "Intrinsics.h"
#include "Square.h"

#include <stdint.h>
#include <time.h>

//#define BITBOARD_SPEED_TESTS

// TODO: Test bitboards to see if a byte lookup indirection is helpful. This would reduce the memory usage by a factor of 7(ish), at the expense of an extra instruction. 2MB->295KB.

struct Context0
{
    uint64_t mask;
    uint64_t magic;
};

static struct Context0 Masks0[64] = { 0 };

struct Context1
{
    uint64_t mask1;
};

static struct Context1 Masks1[64] = { 0 };

struct Context2
{
    uint64_t mask1;
    uint64_t mask2;
};

static struct Context2 Masks2[64] = { 0 };

struct Context3
{
    uint64_t mask1;
    uint64_t mask2;
    uint64_t mask3;
    uint64_t mask4;
};

static struct Context3 Masks3[64] = { 0 };

EncodedSquare MagicBitboard(const uint64_t magicBitboard[64][4096], EncodedSquare occ, Square square)
{
    occ &= Masks0[square].mask;
    occ *= Masks0[square].magic;
    occ >>= 55;
    return magicBitboard[square][occ];
}

EncodedSquare PEXTBitboard1(const uint64_t magicBitboard[64][4096], EncodedSquare occ, Square square)
{
    return magicBitboard[square][intrinsic_pext64(occ, Masks1[square].mask1)];
}

EncodedSquare PEXTBitboard2(const uint64_t magicBitboard[64][64], EncodedSquare occ, Square square)
{
    return magicBitboard[square][intrinsic_pext64(occ, Masks2[square].mask1)] | magicBitboard[square][intrinsic_pext64(occ, Masks2[square].mask2)];
}

EncodedSquare CachelessBitboard(EncodedSquare occ, Square square)
{
    return _blsmsk_u64(occ & Masks3[square].mask1) & Masks3[square].mask1 |
        _blsmsk_u64(occ & Masks3[square].mask2) & Masks3[square].mask2 |
        _blsmsk_u64(occ & Masks3[square].mask3) & Masks3[square].mask3 |
        _blsmsk_u64(occ & Masks3[square].mask4) & Masks3[square].mask4;
}

int main(int argc, char ** argv)
{
    static uint64_t magicLookupFull[64][4096];

    for (uint64_t i = 0; i < 64; ++i)
    {
        for (uint64_t k = 0; k < 4096; k++)
        {
            magicLookupFull[i][k] = (i << 24) + k;
        }
    }

    static uint64_t magicLookupSmall[64][64];

    for (uint64_t i = 0; i < 64; ++i)
    {
        for (uint64_t k = 0; k < 64; k++)
        {
            magicLookupSmall[i][k] = (i << 24) + k;
        }
    }

    for (uint64_t i = 0; i < 64; ++i)
    {
        Masks0[i].mask = 1ull << i;
        Masks0[i].magic = 0xA5ull << i;
        Masks1[i].mask1 = 1ull << i;
        Masks2[i].mask1 = 1ull << i;
        Masks2[i].mask2 = 2ull << i;
        Masks3[i].mask1 = 1ull << i;
        Masks3[i].mask2 = 2ull << i;
        Masks3[i].mask3 = 3ull << i;
        Masks3[i].mask4 = 4ull << i;
    }

    static const int numCycles = 1000000;

    uint64_t sum = 0;
    clock_t begin;
    clock_t end;
    float duration;

    begin = clock();
    for (int cycles = 0; cycles < numCycles; cycles++)
    {
        for (Square i = 0; i < 64; ++i)
        {
            for (EncodedSquare occ = 1; occ < 0x7FFFFFFFFFFFFFFFull; occ <<= 1ull)
                sum += MagicBitboard(magicLookupFull, occ, i);
        }
    }

    end = clock();
    duration = (float) (end - begin) / CLOCKS_PER_SEC;
    printf("MagicBitboard duration: %f seconds\n", duration);

    begin = clock();
    for (int cycles = 0; cycles < numCycles; cycles++)
    {
        for (Square i = 0; i < 64; ++i)
        {
            for (EncodedSquare occ = 1; occ < 0x7FFFFFFFFFFFFFFFull; occ <<= 1ull)
                sum += PEXTBitboard1(magicLookupFull, occ, i);
        }
    }
    end = clock();
    duration = (float) (end - begin) / CLOCKS_PER_SEC;
    printf("PEXTBitboard1 duration: %f seconds\n", duration);

    begin = clock();
    for (int cycles = 0; cycles < numCycles; cycles++)
    {
        for (Square i = 0; i < 64; ++i)
        {
            for (EncodedSquare occ = 1; occ < 0x7FFFFFFFFFFFFFFFull; occ <<= 1ull)
                sum += PEXTBitboard2(magicLookupSmall, occ, i);
        }
    }
    end = clock();
    duration = (float) (end - begin) / CLOCKS_PER_SEC;
    printf("PEXTBitboard2 duration: %f seconds\n", duration);

    begin = clock();
    for (int cycles = 0; cycles < numCycles; cycles++)
    {
        for (Square i = 0; i < 64; ++i)
        {
            for (EncodedSquare occ = 1; occ < 0x7FFFFFFFFFFFFFFFull; occ <<= 1ull)
                sum += CachelessBitboard(occ, i);
        }
    }
    end = clock();
    duration = (float) (end - begin) / CLOCKS_PER_SEC;
    printf("CachelessBitboard duration: %f seconds\n", duration);

    printf("Blah %u", (int) sum);

    return 0;
}
