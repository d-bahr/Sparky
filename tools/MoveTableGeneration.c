#include "File.h"
#include "PieceType.h"
#include "Player.h"
#include "Rank.h"
#include "Square.h"

#include <stdbool.h>
#include <stdio.h>

//#define MOVE_TABLE_GENERATION

int main(int argc, char ** argv)
{
    static const PieceType pieceType = Pawn;
    static const bool pawnCaptures = true;
    static const bool pawnLongMoves = false;
    static const Player player = White;

    // Temporary variables.
    Rank r;
    File f;

    for (Square square = 0; square < 64; ++square)
    {
        EncodedSquare encodedMoves = 0;
        Rank rank = SquareGetRank(square);
        File file = SquareGetFile(square);
        switch (pieceType)
        {
        case Pawn:
            if (pawnLongMoves)
            {
                if (player == White)
                {
                    if (rank != Rank2)
                        break;
                    Square move = SquareFromRankFile(rank + 2, file);
                    encodedMoves |= SquareEncode(move);
                }
                else
                {
                    if (rank != Rank7)
                        break;
                    Square move = SquareFromRankFile(rank - 2, file);
                    encodedMoves |= SquareEncode(move);
                }
            }
            else if (pawnCaptures)
            {
                if (rank == Rank1 || rank == Rank8)
                    break;

                if (player == White)
                {
                    if (file >= FileB)
                    {
                        Square move = SquareFromRankFile(rank + 1, file - 1);
                        encodedMoves |= SquareEncode(move);
                    }
                    if (file <= FileG)
                    {
                        Square move = SquareFromRankFile(rank + 1, file + 1);
                        encodedMoves |= SquareEncode(move);
                    }
                }
                else
                {
                    if (file >= FileB)
                    {
                        Square move = SquareFromRankFile(rank - 1, file - 1);
                        encodedMoves |= SquareEncode(move);
                    }
                    if (file <= FileG)
                    {
                        Square move = SquareFromRankFile(rank - 1, file + 1);
                        encodedMoves |= SquareEncode(move);
                    }
                }
            }
            else
            {
                if (rank == Rank1 || rank == Rank8)
                    break;

                if (player == White)
                {
                    Square move = SquareFromRankFile(rank + 1, file);
                    encodedMoves |= SquareEncode(move);
                }
                else
                {
                    Square move = SquareFromRankFile(rank - 1, file);
                    encodedMoves |= SquareEncode(move);
                }
            }
            break;

        case Knight:
        {
            // Defined in clockwise order.
            Square move1 = SquareFromRankFile(rank + 2, file + 1);
            Square move2 = SquareFromRankFile(rank + 1, file + 2);
            Square move3 = SquareFromRankFile(rank - 1, file + 2);
            Square move4 = SquareFromRankFile(rank - 2, file + 1);
            Square move5 = SquareFromRankFile(rank - 2, file - 1);
            Square move6 = SquareFromRankFile(rank - 1, file - 2);
            Square move7 = SquareFromRankFile(rank + 1, file - 2);
            Square move8 = SquareFromRankFile(rank + 2, file - 1);

            if (file <= FileG && rank <= Rank6)
                encodedMoves |= SquareEncode(move1);
            if (file <= FileF && rank <= Rank7)
                encodedMoves |= SquareEncode(move2);
            if (file <= FileF && rank >= Rank2)
                encodedMoves |= SquareEncode(move3);
            if (file <= FileG && rank >= Rank3)
                encodedMoves |= SquareEncode(move4);
            if (file >= FileB && rank >= Rank3)
                encodedMoves |= SquareEncode(move5);
            if (file >= FileC && rank >= Rank2)
                encodedMoves |= SquareEncode(move6);
            if (file >= FileC && rank <= Rank7)
                encodedMoves |= SquareEncode(move7);
            if (file >= FileB && rank <= Rank6)
                encodedMoves |= SquareEncode(move8);
        }

        break;

        case Bishop:
            r = rank;
            f = file;
            while (r > 0 && f > 0)
            {
                r--;
                f--;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (r > 0 && f < NUM_FILES - 1)
            {
                r--;
                f++;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (r < NUM_RANKS - 1 && f < NUM_FILES - 1)
            {
                r++;
                f++;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (r < NUM_RANKS - 1 && f > 0)
            {
                r++;
                f--;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            break;

        case Rook:
            r = rank;
            f = file;
            while (r > 0)
            {
                r--;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (r < NUM_RANKS - 1)
            {
                r++;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (f > 0)
            {
                f--;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (f < NUM_FILES - 1)
            {
                f++;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            break;

        case Queen:
            // Diagonals
            r = rank;
            f = file;
            while (r > 0 && f > 0)
            {
                r--;
                f--;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (r > 0 && f < NUM_FILES - 1)
            {
                r--;
                f++;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (r < NUM_RANKS - 1 && f < NUM_FILES - 1)
            {
                r++;
                f++;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (r < NUM_RANKS - 1 && f > 0)
            {
                r++;
                f--;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            // Straight
            r = rank;
            f = file;
            while (r > 0)
            {
                r--;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (r < NUM_RANKS - 1)
            {
                r++;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (f > 0)
            {
                f--;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            r = rank;
            f = file;
            while (f < NUM_FILES - 1)
            {
                f++;
                encodedMoves |= SquareEncodeRankFile(r, f);
            }
            break;

        case King:
        {
            // Defined in clockwise order.
            Square move1 = SquareFromRankFile(rank + 1, file);
            Square move2 = SquareFromRankFile(rank + 1, file + 1);
            Square move3 = SquareFromRankFile(rank, file + 1);
            Square move4 = SquareFromRankFile(rank - 1, file + 1);
            Square move5 = SquareFromRankFile(rank - 1, file);
            Square move6 = SquareFromRankFile(rank - 1, file - 1);
            Square move7 = SquareFromRankFile(rank, file - 1);
            Square move8 = SquareFromRankFile(rank + 1, file - 1);

            if (rank <= Rank7)
                encodedMoves |= SquareEncode(move1);
            if (rank <= Rank7 && file <= FileG)
                encodedMoves |= SquareEncode(move2);
            if (file <= FileG)
                encodedMoves |= SquareEncode(move3);
            if (rank >= Rank2 && file <= FileG)
                encodedMoves |= SquareEncode(move4);
            if (rank >= Rank2)
                encodedMoves |= SquareEncode(move5);
            if (rank >= Rank2 && file >= FileB)
                encodedMoves |= SquareEncode(move6);
            if (file >= FileB)
                encodedMoves |= SquareEncode(move7);
            if (rank <= Rank7 && file >= FileB)
                encodedMoves |= SquareEncode(move8);
        }
        break;
        }
        printf("\t0x%016" PRIx64 ",\n", encodedMoves);
    }
    return 0;
}
