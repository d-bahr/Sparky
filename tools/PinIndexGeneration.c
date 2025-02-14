#include "Board.h"
#include "Square.h"

#include <stdio.h>

//#define PIN_INDEX_GENERATION

int main(int argc, char ** argv)
{
    FILE * pinIndicesFile = fopen("PinIndices.c", "w");
    if (pinIndicesFile == NULL)
        return 1;

    fputs("#ifndef PIN_INDICES_H_\n#define PIN_INDICES_H_\n#include <stdint.h>\n// Auto-generated.\nextern const uint8_t s_pinIndices[64][64] =\n{\n", pinIndicesFile);

    for (Square kingSquare = 0; kingSquare < NUM_SQUARES; ++kingSquare)
    {
        fputs("{", pinIndicesFile);
        for (Square pieceSquare = 0; pieceSquare < NUM_SQUARES; ++pieceSquare)
        {
            if (pieceSquare > 0)
                fputs(",", pinIndicesFile);

            if (kingSquare == pieceSquare)
                fputs("0", pinIndicesFile);
            else if (SquareGetFile(kingSquare) == SquareGetFile(pieceSquare))
            {
                if (SquareGetRank(kingSquare) < SquareGetRank(pieceSquare))
                    fputs("0", pinIndicesFile);
                else
                    fputs("4", pinIndicesFile);
            }
            else if (SquareGetRank(kingSquare) == SquareGetRank(pieceSquare))
            {
                if (SquareGetFile(kingSquare) < SquareGetFile(pieceSquare))
                    fputs("2", pinIndicesFile);
                else
                    fputs("6", pinIndicesFile);
            }
            else if (abs((int) SquareGetRank(kingSquare) - (int) SquareGetRank(pieceSquare)) == abs((int) SquareGetFile(kingSquare) - (int) SquareGetFile(pieceSquare)))
            {
                if (SquareGetRank(kingSquare) < SquareGetRank(pieceSquare))
                {
                    if (SquareGetFile(kingSquare) < SquareGetFile(pieceSquare))
                        fputs("1", pinIndicesFile);
                    else
                        fputs("7", pinIndicesFile);
                }
                else
                {
                    if (SquareGetFile(kingSquare) < SquareGetFile(pieceSquare))
                        fputs("3", pinIndicesFile);
                    else
                        fputs("5", pinIndicesFile);
                }
            }
            else
            {
                fputs("0", pinIndicesFile);
            }
        }

        if (kingSquare < NUM_SQUARES - 1)
            fputs("},\n", pinIndicesFile);
        else
            fputs("}\n", pinIndicesFile);
    }

    fputs("};\n#endif // PIN_INDICES_H_\n", pinIndicesFile);

    return 0;
}
