#include "Intrinsics.h"
#include "PieceType.h"
#include "Random.h"
#include "Square.h"
#include "StaticAssert.h"
#include "Zobrist.h"

#include <assert.h>
#include <stdlib.h>

static uint64_t s_zobristPieces[2][NUM_PIECE_TYPES][NUM_SQUARES];
static uint64_t s_zobristEnPassant[NUM_FILES];
static uint64_t s_zobristCastling[16];
static uint64_t s_zobristPlayerToMove[2];

void ZobristGenerate()
{
    for (size_t a = 0; a < 2; ++a)
    {
        for (size_t b = 0; b < NUM_PIECE_TYPES; ++b)
        {
            for (size_t c = 0; c < NUM_SQUARES; ++c)
            {
                s_zobristPieces[a][b][c] = RandomU64();
            }
        }
    }

    for (size_t i = 0; i < NUM_FILES; ++i)
        s_zobristEnPassant[i] = RandomU64();

    s_zobristCastling[0] = 0;
    for (size_t i = 1; i < 16; ++i)
        s_zobristCastling[i] = RandomU64();

    s_zobristPlayerToMove[White] = 0;
    s_zobristPlayerToMove[Black] = RandomU64();
}

static FORCE_INLINE uint64_t ZobristForPieceTable(uint64_t pieceTable, Player player, PieceType piece)
{
    uint64_t hash = 0;
    while (pieceTable)
    {
        hash ^= s_zobristPieces[player][piece - 1][SquareDecodeLowest(pieceTable)];
        pieceTable = intrinsic_blsr64(pieceTable);
    }
    return hash;
}

uint64_t ZobristCalculate(const Board * board)
{
    uint64_t hash = ZobristForPieceTable(board->whitePieceTables[PIECE_TABLE_PAWNS], White, Pawn);
    hash ^= ZobristForPieceTable(board->whitePieceTables[PIECE_TABLE_KNIGHTS], White, Knight);
    hash ^= ZobristForPieceTable(intrinsic_andn64(board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS], board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS]), White, Bishop);
    hash ^= ZobristForPieceTable(intrinsic_andn64(board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS], board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS]), White, Rook);
    hash ^= ZobristForPieceTable(board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] & board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS], White, Queen);
    hash ^= s_zobristPieces[White][King - 1][board->whiteKingSquare];

    hash ^= ZobristForPieceTable(board->blackPieceTables[PIECE_TABLE_PAWNS], Black, Pawn);
    hash ^= ZobristForPieceTable(board->blackPieceTables[PIECE_TABLE_KNIGHTS], Black, Knight);
    hash ^= ZobristForPieceTable(intrinsic_andn64(board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS], board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS]), Black, Bishop);
    hash ^= ZobristForPieceTable(intrinsic_andn64(board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS], board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS]), Black, Rook);
    hash ^= ZobristForPieceTable(board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] & board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS], Black, Queen);
    hash ^= s_zobristPieces[Black][King - 1][board->blackKingSquare];

    if (board->enPassantSquare != SquareInvalid)
    {
        assert(board->enPassantSquare < NUM_SQUARES);
        hash ^= s_zobristEnPassant[SquareGetFile(board->enPassantSquare)];
    }
    hash ^= s_zobristPlayerToMove[board->playerToMove];

    assert(board->castleBits < 16);
    hash ^= s_zobristCastling[board->castleBits];

    return hash;
}

uint64_t ZobristCalculateMaterialHash(const Board * board)
{
    // Note: Kings are always assumed to be on the board, so we can just ignore them for the material hash, which ignores position and only considers quantity of piece type.
    uint64_t hash = s_zobristPieces[White][Pawn - 1][intrinsic_popcnt64(board->whitePieceTables[PIECE_TABLE_PAWNS])];
    hash ^= s_zobristPieces[White][Knight - 1][intrinsic_popcnt64(board->whitePieceTables[PIECE_TABLE_KNIGHTS])];
    hash ^= s_zobristPieces[White][Bishop - 1][intrinsic_popcnt64(intrinsic_andn64(board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS], board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS]))];
    hash ^= s_zobristPieces[White][Rook - 1][intrinsic_popcnt64(intrinsic_andn64(board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS], board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS]))];
    hash ^= s_zobristPieces[White][Queen - 1][intrinsic_popcnt64(board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] & board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS])];

    hash ^= s_zobristPieces[Black][Pawn - 1][intrinsic_popcnt64(board->blackPieceTables[PIECE_TABLE_PAWNS])];
    hash ^= s_zobristPieces[Black][Knight - 1][intrinsic_popcnt64(board->blackPieceTables[PIECE_TABLE_KNIGHTS])];
    hash ^= s_zobristPieces[Black][Bishop - 1][intrinsic_popcnt64(intrinsic_andn64(board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS], board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS]))];
    hash ^= s_zobristPieces[Black][Rook - 1][intrinsic_popcnt64(intrinsic_andn64(board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS], board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS]))];
    hash ^= s_zobristPieces[Black][Queen - 1][intrinsic_popcnt64(board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] & board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS])];
    return hash;
}

// This function takes advantage of the fact that XOR is reversible and shortcuts a complete Zobrist calculation
// by only computing deltas. This function looks kinda long and ugly, but actually saves a ton of time because
// very few branches will actually be taken.
void ZobristMerge(Board * board, Move move)
{
    // Zobrist hash for white is zero, so we only need to XOR with black's zobrist hash,
    // and it will always toggle back and forth correctly.
    board->hash ^= s_zobristPlayerToMove[1];

    // Special handling for castling.
    if (move.piece == King)
    {
        // No matter where the king moves, castling is no longer possible.
        if (board->playerToMove == White)
        {
            uint8_t oldCastlingKey = board->castleBits;
            uint8_t newCastlingKey = oldCastlingKey & BlackCastle; // Mask off white's castling bits.
            board->hash ^= s_zobristCastling[oldCastlingKey];
            board->hash ^= s_zobristCastling[newCastlingKey];
        }
        else
        {
            uint8_t oldCastlingKey = board->castleBits;
            uint8_t newCastlingKey = oldCastlingKey & WhiteCastle; // Mask off black's castling bits.
            board->hash ^= s_zobristCastling[oldCastlingKey];
            board->hash ^= s_zobristCastling[newCastlingKey];
        }
        if (move.from == SquareE1)
        {
            if (move.to == SquareG1)
            {
                board->hash ^= s_zobristPieces[board->playerToMove][Rook - 1][SquareH1];
                board->hash ^= s_zobristPieces[board->playerToMove][Rook - 1][SquareF1];
            }
            else if (move.to == SquareC1)
            {
                board->hash ^= s_zobristPieces[board->playerToMove][Rook - 1][SquareA1];
                board->hash ^= s_zobristPieces[board->playerToMove][Rook - 1][SquareD1];
            }
        }
        else if (move.from == SquareE8)
        {
            if (move.to == SquareG8)
            {
                board->hash ^= s_zobristPieces[board->playerToMove][Rook - 1][SquareH8];
                board->hash ^= s_zobristPieces[board->playerToMove][Rook - 1][SquareF8];
            }
            else if (move.to == SquareC8)
            {
                board->hash ^= s_zobristPieces[board->playerToMove][Rook - 1][SquareA8];
                board->hash ^= s_zobristPieces[board->playerToMove][Rook - 1][SquareD8];
            }
        }
    }
    else if (move.piece == Rook)
    {
        // Reduce castling rights if a rook moves from a corner position.
        uint8_t oldCastlingKey;
        uint8_t newCastlingKey;

        switch (move.from)
        {
        case SquareA1:
            oldCastlingKey = board->castleBits;
            newCastlingKey = oldCastlingKey & ~(WhiteLongCastle);
            board->hash ^= s_zobristCastling[oldCastlingKey];
            board->hash ^= s_zobristCastling[newCastlingKey];
            break;
        case SquareH1:
            oldCastlingKey = board->castleBits;
            newCastlingKey = oldCastlingKey & ~(WhiteShortCastle);
            board->hash ^= s_zobristCastling[oldCastlingKey];
            board->hash ^= s_zobristCastling[newCastlingKey];
            break;
        case SquareA8:
            oldCastlingKey = board->castleBits;
            newCastlingKey = oldCastlingKey & ~(BlackLongCastle);
            board->hash ^= s_zobristCastling[oldCastlingKey];
            board->hash ^= s_zobristCastling[newCastlingKey];
            break;
        case SquareH8:
            oldCastlingKey = board->castleBits;
            newCastlingKey = oldCastlingKey & ~(BlackShortCastle);
            board->hash ^= s_zobristCastling[oldCastlingKey];
            board->hash ^= s_zobristCastling[newCastlingKey];
            break;
        }
    }

    // Quick check for capture; hopefully this will shortcut most of the time.
    if (board->allPieceTables & SquareEncode(move.to))
    {
        PieceType capturedPiece = BoardGetPieceAtSquare(board, move.to);
        if (capturedPiece != None)
        {
            board->hash ^= s_zobristPieces[!board->playerToMove][capturedPiece - 1][move.to];
            uint8_t numPieces = BoardGetNumPieces(board, !board->playerToMove, capturedPiece);
            board->materialHash ^= s_zobristPieces[!board->playerToMove][capturedPiece - 1][numPieces] // Removes hash
                                 ^ s_zobristPieces[!board->playerToMove][capturedPiece - 1][numPieces - 1]; // Adds hash

            // If the captured piece was a corner rook, mask off castling rights.
            if (capturedPiece == Rook)
            {
                uint8_t oldCastlingKey;
                uint8_t newCastlingKey;

                switch (move.to)
                {
                case SquareA1:
                    oldCastlingKey = board->castleBits;
                    newCastlingKey = oldCastlingKey & ~(WhiteLongCastle);
                    board->hash ^= s_zobristCastling[oldCastlingKey];
                    board->hash ^= s_zobristCastling[newCastlingKey];
                    break;
                case SquareH1:
                    oldCastlingKey = board->castleBits;
                    newCastlingKey = oldCastlingKey & ~(WhiteShortCastle);
                    board->hash ^= s_zobristCastling[oldCastlingKey];
                    board->hash ^= s_zobristCastling[newCastlingKey];
                    break;
                case SquareA8:
                    oldCastlingKey = board->castleBits;
                    newCastlingKey = oldCastlingKey & ~(BlackLongCastle);
                    board->hash ^= s_zobristCastling[oldCastlingKey];
                    board->hash ^= s_zobristCastling[newCastlingKey];
                    break;
                case SquareH8:
                    oldCastlingKey = board->castleBits;
                    newCastlingKey = oldCastlingKey & ~(BlackShortCastle);
                    board->hash ^= s_zobristCastling[oldCastlingKey];
                    board->hash ^= s_zobristCastling[newCastlingKey];
                    break;
                }
            }
        }
    }

    // Whether we captured en passant or not, the old state is always removed.
    File enPassantFile = FileA;
    if (board->enPassantSquare != SquareInvalid)
    {
        enPassantFile = SquareGetFile(board->enPassantSquare);
        board->hash ^= s_zobristEnPassant[enPassantFile];
    }
    if (move.piece == Pawn)
    {
        // Special handling for en passant. 
        if (move.to == board->enPassantSquare)
        {
            // Remove captured piece from hash.
            Square removedPawnSquare;
            if (board->playerToMove == White)
            {
                // White en passant always removes a pawn on the 5th rank.
                removedPawnSquare = SquareFromRankFile(Rank5, enPassantFile);
            }
            else
            {
                // White en passant always removes a pawn on the 4th rank.
                removedPawnSquare = SquareFromRankFile(Rank4, enPassantFile);
            }
            board->hash ^= s_zobristPieces[!board->playerToMove][Pawn - 1][removedPawnSquare];
        }
        else if ((board->playerToMove == White && SquareGetRank(move.from) == Rank2 && SquareGetRank(move.to) == Rank4) ||
                 (board->playerToMove == Black && SquareGetRank(move.from) == Rank7 && SquareGetRank(move.to) == Rank5))
        {
            // En passant now possible on the move's file.
            board->hash ^= s_zobristEnPassant[SquareGetFile(move.from)];
        }
        else if (move.promotion != None)
        {
            // Remove the pawn and replace it with the promotion.
            board->hash ^= s_zobristPieces[board->playerToMove][move.piece - 1][move.from];
            board->hash ^= s_zobristPieces[board->playerToMove][move.promotion - 1][move.to];
            uint8_t numPawns = BoardGetNumPieces(board, board->playerToMove, Pawn);
            uint8_t numPieces = BoardGetNumPieces(board, board->playerToMove, move.promotion);
            board->materialHash ^= s_zobristPieces[board->playerToMove][Pawn - 1][numPawns] // Removes hash
                                 ^ s_zobristPieces[board->playerToMove][Pawn - 1][numPawns - 1] // Adds hash
                                 ^ s_zobristPieces[board->playerToMove][move.promotion - 1][numPieces] // Removes hash
                                 ^ s_zobristPieces[board->playerToMove][move.promotion - 1][numPieces + 1]; // Adds hash
            return;
        }
    }

    // The rest of this is standard: move the piece.
    board->hash ^= s_zobristPieces[board->playerToMove][move.piece - 1][move.from];
    board->hash ^= s_zobristPieces[board->playerToMove][move.piece - 1][move.to];
}

void ZobristSwapPlayer(Board * board)
{
    // Zobrist hash for white is zero, so we only need to XOR with black's zobrist hash,
    // and it will always toggle back and forth correctly.
    board->hash ^= s_zobristPlayerToMove[1];

    // Remove en passant if needed.
    if (board->enPassantSquare != SquareInvalid)
        board->hash ^= s_zobristEnPassant[SquareGetFile(board->enPassantSquare)];
}
