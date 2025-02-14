
#include "StaticEval.h"

// Piece tables taken from https://www.chessprogramming.org/PeSTO%27s_Evaluation_Function
const int32_t PieceValuesMillipawnsMidGame[NUM_PIECE_TYPES + 1] = { 0, 820, 3370, 3650, 4770, 10250, 0 };
const int32_t PieceValuesMillipawnsEndGame[NUM_PIECE_TYPES + 1] = { 0, 940, 2810, 2970, 5120,  9360, 0 };
const float MidGameScalar[24] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
const float EndGameScalar[24] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

static const int32_t PieceValueTableMidGame[NUM_PIECE_TYPES + 1][NUM_SQUARES] =
{
    {
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    },
    {
           0,    0,    0,    0,    0,    0,   0,    0,
         980, 1340,  610,  950,  680, 1260, 340, -110,
         -60,   70,  260,  310,  650,  560, 250, -200,
        -140,  130,   60,  210,  230,  120, 170, -230,
        -270,  -20,  -50,  120,  170,   60, 100, -250,
        -260,  -40,  -40, -100,   30,   30, 330, -120,
        -350,  -10, -200, -230, -150,  240, 380, -220,
           0,    0,    0,    0,    0,    0,   0,    0
    },
    {
        -1670, -890, -340, -490,  610, -970, -150, -1070,
         -730, -410,  720,  360,  230,  620,   70,  -170,
         -470,  600,  370,  650,  840, 1290,  730,   440,
          -90,  170,  190,  530,  370,  690,  180,   220,
         -130,   40,  160,  130,  280,  190,  210,   -80,
         -230,  -90,  120,  100,  190,  170,  250,  -160,
         -290, -530, -120,  -30,  -10,  180, -140,  -190,
        -1050, -210, -580, -330, -170, -280, -190,  -230
    },
    {
        -290,   40, -820, -370, -250, -420,   70,  -80,
        -260,  160, -180, -130,  300,  590,  180, -470,
        -160,  370,  430,  400,  350,  500,  370,  -20,
         -40,   50,  190,  500,  370,  370,   70,  -20,
         -60,  130,  130,  260,  340,  120,  100,   40,
           0,  150,  150,  150,  140,  270,  180,  100,
          40,  150,  160,    0,   70,  210,  330,   10,
        -330,  -30, -140, -210, -130, -120, -390, -210
    },
    {
         320,  420,  320,  510, 630,  90,  310,  430,
         270,  320,  580,  620, 800, 670,  260,  440,
         -50,  190,  260,  360, 170, 450,  610,  160,
        -240, -110,   70,  260, 240, 350,  -80, -200,
        -360, -260, -120,  -10,  90, -70,   60, -230,
        -450, -250, -160, -170,  30,   0,  -50, -330,
        -440, -160, -200,  -90, -10, 110,  -60, -710,
        -190, -130,   10,  170, 160,  70, -370, -260
    },
    {
        -280,    0,  290,  120,  590,  440,  430,  450,
        -240, -390,  -50,   10, -160,  570,  280,  540,
        -130, -170,   70,   80,  290,  560,  470,  570,
        -270, -270, -160, -160,  -10,  170,  -20,   10,
         -90, -260,  -90, -100,  -20,  -40,   30,  -30,
        -140,   20, -110,  -20,  -50,   20,  140,   50,
        -350,  -80,  110,   20,   80,  150,  -30,   10,
         -10, -180,  -90,  100, -150, -250, -310, -500
    },
    {
        -650,  230,  160, -150, -560, -340,   20,  130,
         290,  -10, -200,  -70,  -80,  -40, -380, -290,
         -90,  240,   20, -160, -200,   60,  220, -220,
        -170, -200, -120, -270, -300, -250, -140, -360,
        -490,  -10, -270, -390, -460, -440, -330, -510,
        -140, -140, -220, -460, -440, -300, -150, -270,
          10,   70,  -80, -640, -430, -160,   90,   80,
        -150,  360,  120, -540,   80, -280,  240,  140
    }
};

static const int32_t PieceValueTableEndGame[NUM_PIECE_TYPES + 1][NUM_SQUARES] =
{
    {
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    },
    {
           0,    0,    0,    0,    0,    0,    0,    0,
        1780, 1730, 1580, 1340, 1470, 1320, 1650, 1870,
         940, 1000,  850,  670,  560,  530,  820,  840,
         320,  240,  130,   50,  -20,   40,  170,  170,
         130,   90,  -30,  -70,  -70,  -80,   30,  -10,
          40,   70,  -60,   10,    0,  -50,  -10,  -80,
         130,   80,   80,  100,  130,    0,   20,  -70,
           0,    0,    0,    0,    0,    0,    0,    0
    },
    {
        -580, -380, -130, -280, -310, -270, -630, -990,
        -250,  -80, -250,  -20,  -90, -250, -240, -520,
        -240, -200,  100,   90,  -10,  -90, -190, -410,
        -170,   30,  220,  220,  220,  110,   80, -180,
        -180,  -60,  160,  250,  160,  170,   40, -180,
        -230,  -30,  -10,  150,  100,  -30, -200, -220,
        -420, -200, -100,  -50,  -20, -200, -230, -440,
        -290, -510, -230, -150, -220, -180, -500, -640
    },
    {
        -140, -210, -110,  -80, -70,  -90, -170, -240,
         -80,  -40,   70, -120, -30, -130,  -40, -140,
          20,  -80,    0,  -10, -20,   60,    0,   40,
         -30,   90,  120,   90, 140,  100,   30,   20,
         -60,   30,  130,  190,  70,  100,  -30,  -90,
        -120,  -30,   80,  100, 130,   30,  -70, -150,
        -140, -180,  -70,  -10,  40,  -90, -150, -270,
        -230,  -90, -230,  -50, -90, -160,  -50, -170
    },
    {
        130, 100, 180, 150, 120,  120,   80,   50,
        110, 130, 130, 110, -30,   30,   80,   30,
         70,  70,  70,  50,  40,  -30,  -50,  -30,
         40,  30, 130,  10,  20,   10,  -10,   20,
         30,  50,  80,  40, -50,  -60,  -80, -110,
        -40,   0, -50, -10, -70, -120,  -80, -160,
        -60, -60,   0,  20, -90,  -90, -110,  -30,
        -90,  20,  30, -10, -50, -130,   40, -200
    },
    {
         -90,  220,  220,  270,  270,  190,  100,  200,
        -170,  200,  320,  410,  580,  250,  300,    0,
        -200,   60,   90,  490,  470,  350,  190,   90,
          30,  220,  240,  450,  570,  400,  570,  360,
        -180,  280,  190,  470,  310,  340,  390,  230,
        -160, -270,  150,   60,   90,  170,  100,   50,
        -220, -230, -300, -160, -160, -230, -360, -320,
        -330, -280, -220, -430,  -50, -320, -200, -410
    },
    {
        -740, -350, -180, -180, -110,  150,   40, -170,
        -120,  170,  140,  170,  170,  380,  230,  110,
         100,  170,  230,  150,  200,  450,  440,  130,
         -80,  220,  240,  270,  260,  330,  260,   30,
        -180,  -40,  210,  240,  270,  230,   90, -110,
        -190,  -30,  110,  210,  230,  160,   70,  -90,
        -270, -110,   40,  130,  140,   40,  -50, -170,
        -530, -340, -210, -110, -280, -140, -240, -430
    }
};

int32_t g_midGameTables[2][NUM_PIECE_TYPES + 1][NUM_SQUARES];
int32_t g_endGameTables[2][NUM_PIECE_TYPES + 1][NUM_SQUARES];

static inline int32_t GetPieceValues(float midGameProgressionScalar, float endGameProgressionScalar, Player player, PieceType piece, uint64_t pieceTable)
{
    int32_t result = 0;

    // TODO: Profile to see if AVX512 is faster here:
#if 0
    int32_t * valueGrid = PieceValueGrids[piece];
    result += _mm512_mask_reduce_add_epi32((pieceTable & 0x000000000000FFFF), _mm512_load_si512(valueGrid));
    result += _mm512_mask_reduce_add_epi32((pieceTable & 0x00000000FFFF0000) >> 16, _mm512_load_si512(valueGrid + 16));
    result += _mm512_mask_reduce_add_epi32((pieceTable & 0x0000FFFF00000000) >> 32, _mm512_load_si512(valueGrid + 32));
    result += _mm512_mask_reduce_add_epi32((pieceTable & 0xFFFF000000000000) >> 48, _mm512_load_si512(valueGrid + 48));
#else
    while (pieceTable != 0)
    {
        result += GetPieceValue(midGameProgressionScalar, endGameProgressionScalar, player, piece, SquareDecodeLowest(pieceTable));
        pieceTable = intrinsic_blsr64(pieceTable);
    }
#endif

    return result;
}

int32_t Evaluate(const Board * board)
{
    unsigned long long pieceCount = intrinsic_popcnt64(board->allPieceTables);
    if (pieceCount > 23)
        pieceCount = 23;
    float midGameProgressionScalar = MidGameScalar[pieceCount];
    float endGameProgressionScalar = EndGameScalar[pieceCount];
    int32_t score = GetPieceValues(midGameProgressionScalar, endGameProgressionScalar, White, Pawn, board->whitePieceTables[PIECE_TABLE_PAWNS]);
    score += GetPieceValues(midGameProgressionScalar, endGameProgressionScalar, White, Knight, board->whitePieceTables[PIECE_TABLE_KNIGHTS]);
    score += GetPieceValues(midGameProgressionScalar, endGameProgressionScalar, White, Bishop, intrinsic_andn64(board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS], board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS]));
    score += GetPieceValues(midGameProgressionScalar, endGameProgressionScalar, White, Rook, intrinsic_andn64(board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS], board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS]));
    score += GetPieceValues(midGameProgressionScalar, endGameProgressionScalar, White, Queen, board->whitePieceTables[PIECE_TABLE_BISHOPS_QUEENS] & board->whitePieceTables[PIECE_TABLE_ROOKS_QUEENS]);
    score += GetPieceValue(midGameProgressionScalar, endGameProgressionScalar, White, King, board->whiteKingSquare);

    score -= GetPieceValues(midGameProgressionScalar, endGameProgressionScalar, Black, Pawn, board->blackPieceTables[PIECE_TABLE_PAWNS]);
    score -= GetPieceValues(midGameProgressionScalar, endGameProgressionScalar, Black, Knight, board->blackPieceTables[PIECE_TABLE_KNIGHTS]);
    score -= GetPieceValues(midGameProgressionScalar, endGameProgressionScalar, Black, Bishop, intrinsic_andn64(board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS], board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS]));
    score -= GetPieceValues(midGameProgressionScalar, endGameProgressionScalar, Black, Rook, intrinsic_andn64(board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS], board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS]));
    score -= GetPieceValues(midGameProgressionScalar, endGameProgressionScalar, Black, Queen, board->blackPieceTables[PIECE_TABLE_BISHOPS_QUEENS] & board->blackPieceTables[PIECE_TABLE_ROOKS_QUEENS]);
    score -= GetPieceValue(midGameProgressionScalar, endGameProgressionScalar, Black, King, board->blackKingSquare);

    return score;
}

void StaticEvalInitialize()
{
    for (PieceType t = 1; t <= NUM_PIECE_TYPES; ++t)
    {
        for (Square s = 0; s < NUM_SQUARES; ++s)
        {
            // Note: the tables are specified in visual order, which means we need to flip them around for player positions.
            g_midGameTables[White][t][s] = PieceValuesMillipawnsMidGame[t] + PieceValueTableMidGame[t][SQUARE_VERTICAL_FLIP(s)];
            g_endGameTables[White][t][s] = PieceValuesMillipawnsEndGame[t] + PieceValueTableEndGame[t][SQUARE_VERTICAL_FLIP(s)];
            g_midGameTables[Black][t][s] = PieceValuesMillipawnsMidGame[t] + PieceValueTableMidGame[t][SQUARE_HORIZONTAL_FLIP(s)];
            g_endGameTables[Black][t][s] = PieceValuesMillipawnsEndGame[t] + PieceValueTableEndGame[t][SQUARE_HORIZONTAL_FLIP(s)];
        }
    }
}
