#include "Board.h"
#include "MinMax.h"
#include "Square.h"

#include <stdio.h>

//#define IN_BETWEEN_MASK_GENERATION

int main(int argc, char ** argv)
{
    FILE * inBetweenMaskFile = fopen("InBetweenMasks.c", "w");
    if (inBetweenMaskFile == NULL)
        return 1;

    fputs("#ifndef IN_BETWEEN_MASKS_H_\n#define IN_BETWEEN_MASKS_H_\n#include \"Square.h\"\n// Auto-generated.\nextern const EncodedSquare s_inBetweenMask[64][64] =\n{\n", inBetweenMaskFile);

    for (Square start = 0; start < NUM_SQUARES; ++start)
    {
        fputs("{", inBetweenMaskFile);
        for (Square end = 0; end < NUM_SQUARES; ++end)
        {
            if (end > 0)
                fputs(",", inBetweenMaskFile);

            if (start == end)
                fputs("0", inBetweenMaskFile);
            else if (SquareGetRank(start) == SquareGetRank(end))
            {
                EncodedSquare inBetween = 0;
                File f1 = SquareGetFile(start);
                File f2 = SquareGetFile(end);
                for (File i = min(f1, f2); i != max(f1, f2); ++i)
                    inBetween |= SquareEncodeRankFile(SquareGetRank(start), i);
                inBetween &= ~SquareEncode(start);
                inBetween &= ~SquareEncode(end);
                if (inBetween == 0)
                    fputs("0", inBetweenMaskFile);
                else
                    fprintf(inBetweenMaskFile, "0x%" PRIx64, inBetween);
            }
            else if (SquareGetFile(start) == SquareGetFile(end))
            {
                EncodedSquare inBetween = 0;
                Rank r1 = SquareGetRank(start);
                Rank r2 = SquareGetRank(end);
                for (Rank i = min(r1, r2); i != max(r1, r2); ++i)
                    inBetween |= SquareEncodeRankFile(i, SquareGetFile(start));
                inBetween &= ~SquareEncode(start);
                inBetween &= ~SquareEncode(end);
                if (inBetween == 0)
                    fputs("0", inBetweenMaskFile);
                else
                    fprintf(inBetweenMaskFile, "0x%" PRIx64, inBetween);
            }
            else if (abs((int) SquareGetRank(start) - (int) SquareGetRank(end)) == abs((int) SquareGetFile(start) - (int) SquareGetFile(end)))
            {
                EncodedSquare inBetween = 0;
                Rank r, rend;
                File f;
                if (SquareGetRank(start) < SquareGetRank(end))
                {
                    if (SquareGetFile(start) < SquareGetFile(end))
                    {
                        r = SquareGetRank(start);
                        f = SquareGetFile(start);
                        rend = SquareGetRank(end);
                        for (; r < rend; ++r, ++f)
                            inBetween |= SquareEncodeRankFile(r, f);
                    }
                    else
                    {
                        r = SquareGetRank(start);
                        f = SquareGetFile(start);
                        rend = SquareGetRank(end);
                        for (; r < rend; ++r, --f)
                            inBetween |= SquareEncodeRankFile(r, f);
                    }
                }
                else
                {
                    if (SquareGetFile(start) < SquareGetFile(end))
                    {
                        r = SquareGetRank(end);
                        f = SquareGetFile(end);
                        rend = SquareGetRank(start);
                        for (; r < rend; ++r, --f)
                            inBetween |= SquareEncodeRankFile(r, f);
                    }
                    else
                    {
                        r = SquareGetRank(end);
                        f = SquareGetFile(end);
                        rend = SquareGetRank(start);
                        for (; r < rend; ++r, ++f)
                            inBetween |= SquareEncodeRankFile(r, f);
                    }
                    rend = SquareGetRank(start);
                }
                inBetween &= ~SquareEncode(start);
                inBetween &= ~SquareEncode(end);
                if (inBetween == 0)
                    fputs("0", inBetweenMaskFile);
                else
                    fprintf(inBetweenMaskFile, "0x%" PRIx64, inBetween);
            }
            else
            {
                fputs("0", inBetweenMaskFile);
            }
        }

        if (start < NUM_SQUARES - 1)
            fputs("},\n", inBetweenMaskFile);
        else
            fputs("}\n", inBetweenMaskFile);
    }
    fputs("};\n#endif // IN_BETWEEN_MASKS_H_\n", inBetweenMaskFile);

    fclose(inBetweenMaskFile);

    return 0;
}
