#include "Square.h"

#include <stdio.h>
#include <stdint.h>

//#define BITBOARD_GENERATION

int GenerateRookBlockerBitboards()
{
    FILE * of = fopen("rookBlockerBitboards.bin", "wb");
    if (of == NULL)
        return 1;

    //fprintf(of, "static const uint64_t s_rookMoveBitboards[64][4096] = {\n");

    for (Square square = 0; square < 64; ++square)
    {
        File f, f1, f2;
        Rank r, r1, r2;

        f = SquareGetFile(square);
        r = SquareGetRank(square);

        EncodedSquare combined = 0;
        EncodedSquare checkSquare = 0;

        f1 = f;
        while (f1-- > FileB)
            combined |= SquareEncodeRankFile(r, f1);

        f2 = f + 1;
        while (f2 <= FileG)
        {
            combined |= SquareEncodeRankFile(r, f2);
            f2++;
        }

        r1 = r;
        while (r1-- > Rank2)
            combined |= SquareEncodeRankFile(r1, f);

        r2 = r + 1;
        while (r2 <= Rank7)
        {
            combined |= SquareEncodeRankFile(r2, f);
            r2++;
        }

        printf("\t0x%016" PRIx64 "\n", combined);

        //fprintf(of, "{ ");

        uint64_t numBits = intrinsic_popcnt64(combined);
        uint64_t numBoards = 1ull << numBits;
        for (uint64_t i = 0; i < numBoards; ++i)
        {
            uint64_t bitboard = intrinsic_pext64(i, combined);
            uint64_t mask = 0;

            f1 = f;
            while (f1-- > FileA)
            {
                checkSquare = SquareEncodeRankFile(r, f1);
                mask |= checkSquare;
                if (bitboard & checkSquare)
                    break;
            }

            f2 = f + 1;
            while (f2 <= FileH)
            {
                checkSquare = SquareEncodeRankFile(r, f2);
                mask |= checkSquare;
                if (bitboard & checkSquare)
                    break;
                f2++;
            }

            r1 = r;
            while (r1-- > Rank1)
            {
                checkSquare = SquareEncodeRankFile(r1, f);
                mask |= checkSquare;
                if (bitboard & checkSquare)
                    break;
            }

            r2 = r + 1;
            while (r2 <= Rank8)
            {
                checkSquare = SquareEncodeRankFile(r2, f);
                mask |= checkSquare;
                if (bitboard & checkSquare)
                    break;
                r2++;
            }

            fwrite(&mask, sizeof(mask), 1, of);

            /*if (i < numBoards - 1)
                fprintf(of, "0x%" PRIX64 ",", mask);
            else
                fprintf(of, "0x%" PRIX64, mask);*/
        }

        /*if (square < 63)
            fprintf(of, " },\n");
        else
            fprintf(of, "}\n");*/
    }

    //fprintf(of, "};\n");

    fclose(of);

    printf("\n\n");

    return 0;
}

int GenerateBishopBlockerBitboards()
{
    FILE * of = fopen("bishopBlockerBitboards.bin", "wb");
    if (of == NULL)
        return 1;

    for (Square square = 0; square < 64; ++square)
    {
        File f;
        Rank r;

        EncodedSquare combined = 0;
        EncodedSquare checkSquare = 0;

        f = SquareGetFile(square);
        r = SquareGetRank(square);
        while (f-- > FileB && r-- > Rank2)
        {
            combined |= SquareEncodeRankFile(r, f);
        }

        f = SquareGetFile(square) + 1;
        r = SquareGetRank(square);
        while (f <= FileG && r-- > Rank2)
        {
            combined |= SquareEncodeRankFile(r, f);
            f++;
        }

        f = SquareGetFile(square);
        r = SquareGetRank(square) + 1;
        while (f-- > FileB && r <= Rank7)
        {
            combined |= SquareEncodeRankFile(r, f);
            r++;
        }

        f = SquareGetFile(square) + 1;
        r = SquareGetRank(square) + 1;
        while (f <= FileG && r <= Rank7)
        {
            combined |= SquareEncodeRankFile(r, f);
            r++;
            f++;
        }

        printf("\t0x%016" PRIx64 "\n", combined);

        //fprintf(of, "{ ");

        uint64_t numBits = intrinsic_popcnt64(combined);
        uint64_t numBoards = 1ull << numBits;
        for (uint64_t i = 0; i < numBoards; ++i)
        {
            uint64_t bitboard = intrinsic_pext64(i, combined);
            uint64_t mask = 0;

            f = SquareGetFile(square);
            r = SquareGetRank(square);
            while (f-- > FileA && r-- > Rank1)
            {
                checkSquare = SquareEncodeRankFile(r, f);
                mask |= checkSquare;
                if (bitboard & checkSquare)
                    break;
            }

            f = SquareGetFile(square) + 1;
            r = SquareGetRank(square);
            while (f <= FileH && r-- > Rank1)
            {
                checkSquare = SquareEncodeRankFile(r, f);
                mask |= checkSquare;
                if (bitboard & checkSquare)
                    break;
                f++;
            }

            f = SquareGetFile(square);
            r = SquareGetRank(square) + 1;
            while (f-- > FileA && r <= Rank8)
            {
                checkSquare = SquareEncodeRankFile(r, f);
                mask |= checkSquare;
                if (bitboard & checkSquare)
                    break;
                r++;
            }

            f = SquareGetFile(square) + 1;
            r = SquareGetRank(square) + 1;
            while (f <= FileH && r <= Rank8)
            {
                checkSquare = SquareEncodeRankFile(r, f);
                mask |= checkSquare;
                if (bitboard & checkSquare)
                    break;
                r++;
                f++;
            }

            fwrite(&mask, sizeof(mask), 1, of);

            /*if (i < numBoards - 1)
                fprintf(of, "0x%" PRIX64 ",", mask);
            else
                fprintf(of, "0x%" PRIX64, mask);*/
        }

        /*if (square < 63)
            fprintf(of, " },\n");
        else
            fprintf(of, "}\n");*/
    }

    //fprintf(of, "};\n");

    fclose(of);

    printf("\n\n");

    return 0;
}

int main(int argc, char ** argv)
{
    int result = GenerateRookBlockerBitboards();
    if (result != 0)
        return result;
    return GenerateBishopBlockerBitboards();
}
