#include "FEN.h"
#include "StaticEval.h"
#include "Zobrist.h"

#include <stdio.h>

extern bool ParseRank(char c, Rank * r)
{
    switch (c)
    {
    case '1':
        *r = Rank1;
        return true;
    case '2':
        *r = Rank2;
        return true;
    case '3':
        *r = Rank3;
        return true;
    case '4':
        *r = Rank4;
        return true;
    case '5':
        *r = Rank5;
        return true;
    case '6':
        *r = Rank6;
        return true;
    case '7':
        *r = Rank7;
        return true;
    case '8':
        *r = Rank8;
        return true;
    default:
        return false;
    }
}

extern bool ParseFile(char c, File * f)
{
    switch (c)
    {
    case 'a':
    case 'A':
        *f = FileA;
        return true;
    case 'b':
    case 'B':
        *f = FileB;
        return true;
    case 'c':
    case 'C':
        *f = FileC;
        return true;
    case 'd':
    case 'D':
        *f = FileD;
        return true;
    case 'e':
    case 'E':
        *f = FileE;
        return true;
    case 'f':
    case 'F':
        *f = FileF;
        return true;
    case 'g':
    case 'G':
        *f = FileG;
        return true;
    case 'h':
    case 'H':
        *f = FileH;
        return true;
    default:
        return false;
    }
}

extern bool ParsePieceType(char c, PieceType * type)
{
    switch (c)
    {
    case 'b':
    case 'B':
        *type = Bishop;
        return true;
    case 'k':
    case 'K':
        *type = King;
        return true;
    case 'n':
    case 'N':
        *type = Knight;
        return true;
    case 'p':
    case 'P':
        *type = Pawn;
        return true;
    case 'q':
    case 'Q':
        *type = Queen;
        return true;
    case 'r':
    case 'R':
        *type = Rook;
        return true;
    default:
        return false;
    }
}

extern bool ParseSquare(const char * str, Square * square)
{
    Rank r;
    File f;

    if (!ParseFile(str[0], &f))
        return false;
    if (!ParseRank(str[1], &r))
        return false;

    *square = SquareFromRankFile(r, f);
    return true;
}

extern bool ParseSquareAdvance(const char ** str, Square * square)
{
    if (str == NULL || *str == NULL)
        return false;

    Rank r;
    File f;

    if (!ParseFile(**str, &f))
        return false;
    (*str)++;
    if (!ParseRank(**str, &r))
        return false;
    (*str)++;

    *square = SquareFromRankFile(r, f);
    return true;
}

extern bool ParseMove(const char * str, Move * move)
{
    Square from;
    Square to;
    PieceType promotion = None;
    if (!ParseSquare(str, &from))
        return false;
    if (!ParseSquare(str + 2, &to))
        return false;
    if (str[4] != '\0')
    {
        if (!ParsePieceType(str[4], &promotion))
            return false;
    }
    move->from = from;
    move->to = to;
    move->promotion = promotion;
    return true;
}

extern size_t MoveToString(Move move, char * str, size_t len)
{
    if (len < 4)
        return 0;

    str[0] = SquareGetFile(move.from) + 'a';
    str[1] = SquareGetRank(move.from) + '1';
    str[2] = SquareGetFile(move.to) + 'a';
    str[3] = SquareGetRank(move.to) + '1';

    if (move.piece == Pawn && move.promotion != None)
    {
        if (len < 5)
            return 0;
        switch (move.promotion)
        {
        case Knight:
            str[4] = 'n';
            break;
        case Bishop:
            str[4] = 'b';
            break;
        case Rook:
            str[4] = 'r';
            break;
        case Queen:
            str[4] = 'q';
            break;
        }
        return 5;
    }
    else
    {
        return 4;
    }
}

extern bool ParseIntegerFromString(const char ** str, uint32_t * value)
{
    if (!str || !*str || !value)
        return false;

    // Default condition.
    *value = 0;

    bool valid = false;
    do
    {
        uint32_t k;
        switch (**str)
        {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            k = **str - '0';
            break;
        default:
            return valid;
        }

        if (*value > 429496729 ||
            *value == 429496729 && k > 5) // Catch overflow before it happens
            return false;

        *value *= 10;
        *value += k;
        valid = true;

        if ((*str) + 1)
            (*str)++;
        else
            break;
    }
    while (true);

    return valid;
}

extern size_t IntegerToString(char * str, size_t len, int num)
{
    return snprintf(str, len, "%i", num);
}

extern bool ParseFEN(const char * fen, Board * board)
{
    typedef enum FENField
    {
        FENPositions,
        FENActivePlayer,
        FENCastlingRights,
        FENEnPassantTarget,
        FENHalfmoveClock,
        FENFullmoveNumber
    } FENField;

    Rank rank = Rank8;
    File file = FileA;
    FENField field = FENPositions;

    // Default conditions.
    BoardInitializeEmpty(board);
    board->playerToMove = White;

    char c;
    while ((c = *fen) != 0)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            if (field == FENFullmoveNumber)
                return false; // Too many fields.
            else if (field == FENPositions)
            {
                if (file != FileH + 1 || rank != Rank1)
                    return false;

                // Fill out the various piece table combinations.
                board->whitePieceTables[PIECE_TABLE_COMBINED] = board->whitePieceTables[PIECE_TABLE_PAWNS] |
                    board->whitePieceTables[PIECE_TABLE_KNIGHTS] |
                    board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] |
                    board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] |
                    SquareEncode(board->whiteKingSquare);
                board->blackPieceTables[PIECE_TABLE_COMBINED] = board->blackPieceTables[PIECE_TABLE_PAWNS] |
                    board->blackPieceTables[PIECE_TABLE_KNIGHTS] |
                    board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] |
                    board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] |
                    SquareEncode(board->blackKingSquare);
                board->allPieceTables = board->whitePieceTables[PIECE_TABLE_COMBINED] | board->blackPieceTables[PIECE_TABLE_COMBINED];
            }

            field++;
        }
        else
        {
            switch (field)
            {
            case FENPositions:
                if (c == '/') // Next rank
                {
                    if (file != FileH + 1 || rank == Rank1)
                        return false;
                    rank--;
                    file = FileA;
                }
                else
                {
                    if (file > FileH)
                        return false;

                    EncodedSquare square = SquareEncodeRankFile(rank, file);

                    switch (c)
                    {
                    case 'b': // Black bishop
                        board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= square;
                        break;
                    case 'k': // Black king
                        board->blackKingSquare = SquareFromRankFile(rank, file);
                        break;
                    case 'n': // Black knight
                        board->blackPieceTables[PIECE_TABLE_KNIGHTS] |= square;
                        break;
                    case 'p': // Black pawn
                        board->blackPieceTables[PIECE_TABLE_PAWNS] |= square;
                        break;
                    case 'q': // Black queen
                        board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= square;
                        board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] |= square;
                        break;
                    case 'r': // Black rook
                        board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS] |= square;
                        break;
                    case 'B': // White bishop
                        board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= square;
                        break;
                    case 'K': // White king
                        board->whiteKingSquare = SquareFromRankFile(rank, file);
                        break;
                    case 'N': // White knight
                        board->whitePieceTables[PIECE_TABLE_KNIGHTS] |= square;
                        break;
                    case 'P': // White pawn
                        board->whitePieceTables[PIECE_TABLE_PAWNS] |= square;
                        break;
                    case 'Q': // White queen
                        board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] |= square;
                        board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] |= square;
                        break;
                    case 'R': // White rook
                        board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS] |= square;
                        break;
                    case '1': // Empty square(s)
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                        file += (c - '1');
                        break;
                    default:
                        return false;
                    }

                    file++;
                }
                break;
            case FENActivePlayer:
                switch (c)
                {
                case 'w':
                case 'W':
                case '-':
                    board->playerToMove = White;
                    break;
                case 'b':
                case 'B':
                    board->playerToMove = Black;
                    break;
                default:
                    return false;
                }
                break;
            case FENCastlingRights:
                switch (c)
                {
                case 'h': // Shredder-FEN Extension (SMK-FEN)
                case 'k':
                    board->castleBits |= BlackShortCastle;
                    break;
                case 'a': // Shredder-FEN Extension (SMK-FEN)
                case 'q':
                    board->castleBits |= BlackLongCastle;
                    break;
                case 'H': // Shredder-FEN Extension (SMK-FEN)
                case 'K':
                    board->castleBits |= WhiteShortCastle;
                    break;
                case 'A': // Shredder-FEN Extension (SMK-FEN)
                case 'Q':
                    board->castleBits |= WhiteLongCastle;
                    break;
                case '-':
                    board->castleBits = NoCastle;
                    break;
                default:
                    return false;
                }
                break;
            case FENEnPassantTarget:
                if (c == '-')
                {
                    board->enPassantSquare = SquareInvalid;
                }
                else
                {
                    Square enPassantTarget = 0;
                    if (!ParseSquareAdvance(&fen, &enPassantTarget))
                        return false;
                    fen--;
                    board->enPassantSquare = enPassantTarget;
                }
                break;
            case FENHalfmoveClock:
                if (c == '-')
                {
                    board->halfmoveCounter = 0;
                }
                else
                {
                    uint32_t halfmoveCounter = 0;
                    if (!ParseIntegerFromString(&fen, &halfmoveCounter))
                        return false;
                    fen--;
                    if (halfmoveCounter > 0xFF)
                        return false;
                    board->halfmoveCounter = (uint8_t) halfmoveCounter;
                }
                break;
            case FENFullmoveNumber:
                if (c != '-')
                {
                    uint32_t fullmoveNumber = 0;
                    if (!ParseIntegerFromString(&fen, &fullmoveNumber))
                        return false;
                    fen--;
                    if (fullmoveNumber > 0xFFFF)
                        return false;
                    board->ply = (uint16_t) fullmoveNumber;
                }
                break;
            }
        }

        fen++;
    }

    board->hash = ZobristCalculate(board);
    board->materialHash = ZobristCalculateMaterialHash(board);
    board->staticEval = Evaluate(board);

    return file == FileH + 1 && rank == Rank1;
}

static size_t WriteChar(char * str, size_t len, char c, char trailingChar)
{
    if (len < 1)
        return 0;

    str[0] = c;

    if (trailingChar)
    {
        if (len < 2)
            return 1;

        str[1] = trailingChar;
        return 2;
    }
    else
    {
        return 1;
    }
}

static size_t RankToFEN(const Board * board, Rank rank, char * str, size_t len, bool trailingSlash)
{
    size_t k = 0;
    int consecutiveEmpty = 0;
    for (File file = 0; file < NUM_FILES; ++file)
    {
        if (k >= len)
            return k;

        Square square = SquareFromRankFile(rank, file);
        PieceType piece = None;
        Player player = White;
        BoardGetPlayerPieceAtSquare(board, square, &piece, &player);
        
        if (piece == None)
        {
            ++consecutiveEmpty;
        }
        else
        {
            if (consecutiveEmpty > 0)
            {
                str[k++] = (char)(consecutiveEmpty + '0');
                consecutiveEmpty = 0;
            }
            
            switch (piece)
            {
            case Pawn:
                str[k++] = (player == White) ? 'P' : 'p';
                break;
            case Knight:
                str[k++] = (player == White) ? 'N' : 'n';
                break;
            case Bishop:
                str[k++] = (player == White) ? 'B' : 'b';
                break;
            case Rook:
                str[k++] = (player == White) ? 'R' : 'r';
                break;
            case Queen:
                str[k++] = (player == White) ? 'Q' : 'q';
                break;
            case King:
                str[k++] = (player == White) ? 'K' : 'k';
                break;
            }
        }
    }

    if (consecutiveEmpty > 0)
    {
        if (k >= len)
            return k;

        str[k++] = (char) (consecutiveEmpty + '0');
        consecutiveEmpty = 0;
    }

    if (trailingSlash)
    {
        if (k >= len)
            return k;

        str[k++] = '/';
    }

    return k;
}

size_t ToFEN(const Board * board, char * str, size_t len)
{
    size_t k = 0;
    for (int rank = Rank8; rank >= Rank1; --rank)
    {
        k += RankToFEN(board, (Rank) rank, &str[k], len - k, rank != Rank1);
        if (k >= len)
            return k;
    }
    str[k++] = ' ';
    if (k >= len)
        return k;
    k += WriteChar(&str[k], len - k, (board->playerToMove == White) ? 'w' : 'b', ' ');
    if (k >= len)
        return k;
    if (board->castleBits)
    {
        if (board->castleBits & WhiteShortCastle)
        {
            k += WriteChar(&str[k], len - k, 'K',  '\0');
            if (k >= len)
                return k;
        }
        if (board->castleBits & WhiteLongCastle)
        {
            k += WriteChar(&str[k], len - k, 'Q', '\0');
            if (k >= len)
                return k;
        }
        if (board->castleBits & BlackShortCastle)
        {
            k += WriteChar(&str[k], len - k, 'k', '\0');
            if (k >= len)
                return k;
        }
        if (board->castleBits & BlackLongCastle)
        {
            k += WriteChar(&str[k], len - k, 'q', '\0');
            if (k >= len)
                return k;
        }
        str[k++] = ' ';
        if (k >= len)
            return k;
    }
    else
    {
        k += WriteChar(&str[k], len - k, '-', ' ');
        if (k >= len)
            return k;
    }
    if (board->enPassantSquare != SquareInvalid)
    {
        str[k++] = SquareGetFile(board->enPassantSquare) + 'a';
        if (k >= len)
            return k;
        str[k++] = SquareGetRank(board->enPassantSquare) + '1';
        if (k >= len)
            return k;
        str[k++] = ' ';
        if (k >= len)
            return k;
    }
    else
    {
        k += WriteChar(&str[k], len - k, '-', ' ');
        if (k >= len)
            return k;
    }
    k += IntegerToString(&str[k], len - k, board->halfmoveCounter);
    if (k >= len)
        return k;
    str[k++] = ' ';
    if (k >= len)
        return k;
    k += IntegerToString(&str[k], len - k, board->ply);
    return k;
}
