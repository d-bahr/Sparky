#include "Board.h"
#include "StaticEval.h"
#include "Zobrist.h"

void BoardInitializeStartingPosition(Board * board)
{
    board->whitePieceTables[PIECE_TABLE_PAWNS] = 0x000000000000FF00;
    board->whitePieceTables[PIECE_TABLE_KNIGHTS] = 0x0000000000000042;
    board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] = 0x000000000000002C;
    board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] = 0x0000000000000089;
    board->whitePieceTables[PIECE_TABLE_COMBINED] = 0x000000000000FFFF;

    board->blackPieceTables[PIECE_TABLE_PAWNS] = 0x00FF000000000000;
    board->blackPieceTables[PIECE_TABLE_KNIGHTS] = 0x4200000000000000;
    board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] = 0x2C00000000000000;
    board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] = 0x8900000000000000;
    board->blackPieceTables[PIECE_TABLE_COMBINED] = 0xFFFF000000000000;

    board->whiteKingSquare = SquareE1;
    board->blackKingSquare = SquareE8;

    board->allPieceTables = 0xFFFF00000000FFFF;

    board->enPassantSquare = SquareInvalid;
    board->ply = 0;
    board->halfmoveCounter = 0;

    board->castleBits = AllCastle;

    board->playerToMove = White;
    board->hash = ZobristCalculate(board);
    board->materialHash = ZobristCalculateMaterialHash(board);

    board->staticEval = Evaluate(board);
}

// In this function we don't care about the player, so we can combine some checks.
// This will generally be faster (fewer branch mispredictions are possible).
PieceType BoardGetPieceAtSquare(const Board * board, Square square)
{
    EncodedSquare encoded = SquareEncode(square);
    if (encoded & (board->whitePieceTables[PIECE_TABLE_PAWNS] | board->blackPieceTables[PIECE_TABLE_PAWNS]))
        return Pawn;
    else if (encoded & (board->whitePieceTables[PIECE_TABLE_KNIGHTS] | board->blackPieceTables[PIECE_TABLE_KNIGHTS]))
        return Knight;
    else if (encoded & (board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] | board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS]))
    {
        if (encoded & (board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] | board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS]))
            return Queen;
        else
            return Bishop;
    }
    else if (encoded & (board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] | board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS]))
        return Rook;
    else if (square == board->whiteKingSquare || square == board->blackKingSquare)
        return King;
    else
        return None;
}

void BoardGetPlayerPieceAtSquare(const Board * board, Square square, PieceType * type, Player * player)
{
    EncodedSquare encoded = SquareEncode(square);
    if (encoded & board->blackPieceTables[PIECE_TABLE_PAWNS])
    {
        *type = Pawn;
        *player = Black;
    }
    else if (encoded & board->blackPieceTables[PIECE_TABLE_KNIGHTS])
    {
        *type = Knight;
        *player = Black;
    }
    else if (encoded & board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS])
    {
        if (encoded & board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS])
        {
            *type = Queen;
            *player = Black;
        }
        else
        {
            *type = Bishop;
            *player = Black;
        }
    }
    else if (encoded & board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS])
    {
        *type = Rook;
        *player = Black;
    }
    else if (square == board->blackKingSquare)
    {
        *type = King;
        *player = Black;
    }
    else if (encoded & board->whitePieceTables[PIECE_TABLE_PAWNS])
    {
        *type = Pawn;
        *player = White;
    }
    else if (encoded & board->whitePieceTables[PIECE_TABLE_KNIGHTS])
    {
        *type = Knight;
        *player = White;
    }
    else if (encoded & board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS])
    {
        if (encoded & board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS])
        {
            *type = Queen;
            *player = White;
        }
        else
        {
            *type = Bishop;
            *player = White;
        }
    }
    else if (encoded & board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS])
    {
        *type = Rook;
        *player = White;
    }
    else if (square == board->whiteKingSquare)
    {
        *type = King;
        *player = White;
    }
    else
    {
        *type = None;
    }
}

uint64_t BoardGetPieceTable(const Board * board, Player player, PieceType type)
{
    switch (type)
    {
    case Pawn:
        return player == White ? board->whitePieceTables[PIECE_TABLE_PAWNS] : board->blackPieceTables[PIECE_TABLE_PAWNS];
    case Knight:
        return player == White ? board->whitePieceTables[PIECE_TABLE_KNIGHTS] : board->blackPieceTables[PIECE_TABLE_KNIGHTS];
    case Bishop:
        return player == White ?
            intrinsic_andn64(board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS], board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS]) :
            intrinsic_andn64(board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS], board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS]);
    case Rook:
        return player == White ?
            intrinsic_andn64(board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS], board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS]) :
            intrinsic_andn64(board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS], board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS]);
    case Queen:
        return player == White ?
            (board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] & board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS]) :
            (board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] & board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS]);
    case King:
        return player == White ? SquareEncode(board->whiteKingSquare) : SquareEncode(board->blackKingSquare);
    default:
        return 0;
    }
}

uint8_t BoardGetNumPieces(const Board * board, Player player, PieceType type)
{
    if (type == King)
        return 1;
    else
        return (uint8_t)intrinsic_popcnt64(BoardGetPieceTable(board, player, type));
}
