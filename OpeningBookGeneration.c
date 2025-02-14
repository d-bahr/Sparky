#include "FEN.h"
#include "Init.h"
#include "MoveGeneration.h"
#include "OpeningBook.h"
#include "Zobrist.h"

#include <stdio.h>
#include <stdlib.h>

#define LONG_LINES 0

typedef struct
{
    const char * fen;
    Move nextMove;
    uint64_t hash;
} FENOpening;

// Unsorted list of openings. These are compiled from various sources.
// Certain positions have multiple openings; these are chosen at random at runtime.
static FENOpening s_openingBook[] =
{
    // Italian (e4 e5 Nf3 Nc6 Bc5)
    { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0", { SquareE2, SquareE4, Pawn, None } },
    { "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", { SquareE7, SquareE5, Pawn, None } },
    { "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 3", { SquareB8, SquareC6, Knight, None } },
    { "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 4", { SquareF1, SquareC4, Bishop, None } },

    // Italian continuation: Two knights defense
    { "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 5", { SquareG8, SquareF6, Knight, None } },
    { "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 6", { SquareD2, SquareD3, Pawn, None } },
    { "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 6", { SquareB1, SquareC3, Knight, None } },

    // Italian continuation: Giuoco piano
    { "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 5", { SquareF8, SquareC5, Bishop, None } },
    { "r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 6", { SquareC2, SquareC3, Pawn, None } },
    { "r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 6", { SquareD2, SquareD3, Pawn, None } },
    { "r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 6", { SquareE1, SquareG1, King, None } },

    // Italian continuation: d6
    { "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 5", { SquareD7, SquareD6, Pawn, None } },
    { "r1bqkbnr/ppp2ppp/2np4/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 6", { SquareC2, SquareC3, Pawn, None } },
    { "r1bqkbnr/ppp2ppp/2np4/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 6", { SquareE1, SquareG1, King, None } },
    { "r1bqkbnr/ppp2ppp/2np4/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 6", { SquareD2, SquareD4, Pawn, None } },

    // Ruy Lopez (e4 e5 Nf3 Nc6 Bc5)
    { "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 4", { SquareF1, SquareB5, Bishop, None } },

    // Ruy Lopez continuation: Main line
    { "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 5", { SquareA7, SquareA6, Pawn, None } },
    { "r1bqkbnr/1ppp1ppp/p1n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 6", { SquareB5, SquareA4, Bishop, None } },
    { "r1bqkbnr/1ppp1ppp/p1n5/4p3/B3P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 1 7", { SquareG8, SquareF6, Knight, None } },
    { "r1bqkb1r/1ppp1ppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 2 8", { SquareE1, SquareG1, King, None } },

    // Ruy Lopez continuation: Marshall variation
    { "r1bqkb1r/1ppp1ppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 b kq - 3 9", { SquareF8, SquareE7, Bishop, None } },
    { "r1bqk2r/1pppbppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 4 10", { SquareF1, SquareE1, Rook, None } },
    { "r1bqk2r/1pppbppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQR1K1 b kq - 5 11", { SquareB7, SquareB5, Pawn, None } },
    { "r1bqk2r/2ppbppp/p1n2n2/1p2p3/B3P3/5N2/PPPP1PPP/RNBQR1K1 w kq b6 0 12", { SquareA4, SquareB3, Bishop, None } },
    { "r1bqk2r/2ppbppp/p1n2n2/1p2p3/4P3/1B3N2/PPPP1PPP/RNBQR1K1 b kq - 1 13", { SquareE8, SquareG8, King, None } },
    { "r1bq1rk1/2ppbppp/p1n2n2/1p2p3/4P3/1B3N2/PPPP1PPP/RNBQR1K1 w - - 2 14", { SquareC2, SquareC3, Pawn, None } },
    { "r1bq1rk1/2ppbppp/p1n2n2/1p2p3/4P3/1BP2N2/PP1P1PPP/RNBQR1K1 b - - 0 15", { SquareD7, SquareD5, Pawn, None } },

    // Ruy Lopez continuation: Arkhangelsk variation
    { "r1bqkb1r/1ppp1ppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 b kq - 3 9", { SquareB7, SquareB5, Pawn, None } },
    { "r1bqkb1r/2pp1ppp/p1n2n2/1p2p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 w kq b6 0 10", { SquareA4, SquareB3, Bishop, None } },
    { "r1bqkb1r/2pp1ppp/p1n2n2/1p2p3/4P3/1B3N2/PPPP1PPP/RNBQ1RK1 b kq - 1 11", { SquareC8, SquareB7, Bishop, None } },

    // Ruy Lopez continuation: Berlin defense
    { "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 5", { SquareG8, SquareF6, Knight, None } },
#if LONG_LINES
    { "r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 6", { SquareE1, SquareG1, King, None } },
    { "r1bqkb1r/pppp1ppp/2n2n2/1B2p3/4P3/5N2/PPPP1PPP/RNBQ1RK1 b kq - 5 7", { SquareF6, SquareE4, Knight, None } },
    { "r1bqkb1r/pppp1ppp/2n5/1B2p3/4n3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 0 8", { SquareD2, SquareD4, Pawn, None } },
    { "r1bqkb1r/pppp1ppp/2n5/1B2p3/3Pn3/5N2/PPP2PPP/RNBQ1RK1 b kq d3 0 9", { SquareE4, SquareD6, Knight, None } },
    { "r1bqkb1r/pppp1ppp/2nn4/1B2p3/3P4/5N2/PPP2PPP/RNBQ1RK1 w kq - 1 10", { SquareB5, SquareC6, Bishop, None } },
    { "r1bqkb1r/pppp1ppp/2Bn4/4p3/3P4/5N2/PPP2PPP/RNBQ1RK1 b kq - 0 11", { SquareD7, SquareC6, Pawn, None } },
    { "r1bqkb1r/ppp2ppp/2pn4/4p3/3P4/5N2/PPP2PPP/RNBQ1RK1 w kq - 0 12", { SquareD4, SquareE5, Pawn, None } },
    { "r1bqkb1r/ppp2ppp/2pn4/4P3/8/5N2/PPP2PPP/RNBQ1RK1 b kq - 0 13", { SquareD6, SquareF5, Knight, None } },
    { "r1bqkb1r/ppp2ppp/2p5/4Pn2/8/5N2/PPP2PPP/RNBQ1RK1 w kq - 1 14", { SquareD1, SquareD8, Queen, None } },
    { "r1bQkb1r/ppp2ppp/2p5/4Pn2/8/5N2/PPP2PPP/RNB2RK1 b kq - 0 15", { SquareE8, SquareD8, King, None } },
#endif

    // Ruy Lopez continuation: Exchange variation
    { "r1bqkbnr/1ppp1ppp/p1n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 6", { SquareB5, SquareC6, Bishop, None } },
    { "r1bqkbnr/1ppp1ppp/p1B5/4p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 0 7", { SquareD7, SquareC6, Pawn, None } },

    // Ruy Lopez continuation: Open variation
    { "r1bqkb1r/1ppp1ppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 b kq - 3 9", { SquareF6, SquareE4, Knight, None } },
    { "r1bqkb1r/1ppp1ppp/p1n5/4p3/B3n3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 0 10", { SquareD2, SquareD4, Pawn, None } },

    // Ruy Lopez continuation: Schliemann-Jaenisch gambit
    { "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 5", { SquareF7, SquareF5, Pawn, None } },

    // Sicilian (e4 c5)
    { "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", { SquareC7, SquareC5, Pawn, None } },

    // Sicilian continuation: Najdorf variation
    { "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 3", { SquareD7, SquareD6, Pawn, None } },
    { "rnbqkbnr/pp2pppp/3p4/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 4", { SquareD2, SquareD4, Pawn, None } },
    { "rnbqkbnr/pp2pppp/3p4/2p5/3PP3/5N2/PPP2PPP/RNBQKB1R b KQkq d3 0 5", { SquareC5, SquareD4, Pawn, None } },
    { "rnbqkbnr/pp2pppp/3p4/8/3pP3/5N2/PPP2PPP/RNBQKB1R w KQkq - 0 6", { SquareF3, SquareD4, Knight, None } },
    { "rnbqkbnr/pp2pppp/3p4/8/3NP3/8/PPP2PPP/RNBQKB1R b KQkq - 0 7", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/pp2pppp/3p1n2/8/3NP3/8/PPP2PPP/RNBQKB1R w KQkq - 1 8", { SquareB1, SquareC3, Knight, None } },
    { "rnbqkb1r/pp2pppp/3p1n2/8/3NP3/2N5/PPP2PPP/R1BQKB1R b KQkq - 2 9", { SquareA7, SquareA6, Pawn, None } },

    // Sicilian continuation: Dragon variation
    { "rnbqkb1r/pp2pppp/3p1n2/8/3NP3/2N5/PPP2PPP/R1BQKB1R b KQkq - 2 9", { SquareG7, SquareG6, Pawn, None } },

    // Sicilian continuation: Classical variation
    { "rnbqkb1r/pp2pppp/3p1n2/8/3NP3/2N5/PPP2PPP/R1BQKB1R b KQkq - 2 9", { SquareB8, SquareC6, Knight, None } },

    // Sicilian continuation: Alapin variation
    { "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2", { SquareC2, SquareC3, Pawn, None } },

    // Sicilian continuation: French variation
    { "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 3", { SquareE7, SquareE6, Pawn, None } },

    // Sicilian continuation: Sveshnikov variation
    { "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 3", { SquareB8, SquareC6, Knight, None } },
#if LONG_LINES
    { "r1bqkbnr/pp1ppppp/2n5/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 4", { SquareD2, SquareD4, Pawn, None } },
    { "r1bqkbnr/pp1ppppp/2n5/2p5/3PP3/5N2/PPP2PPP/RNBQKB1R b KQkq d3 0 5", { SquareC5, SquareD4, Pawn, None } },
    { "r1bqkbnr/pp1ppppp/2n5/8/3pP3/5N2/PPP2PPP/RNBQKB1R w KQkq - 0 6", { SquareF3, SquareD4, Knight, None } },
    { "r1bqkbnr/pp1ppppp/2n5/8/3NP3/8/PPP2PPP/RNBQKB1R b KQkq - 0 7", { SquareG8, SquareF6, Knight, None } },
    { "r1bqkb1r/pp1ppppp/2n2n2/8/3NP3/8/PPP2PPP/RNBQKB1R w KQkq - 1 8", { SquareB1, SquareC3, Knight, None } },
    { "r1bqkb1r/pp1ppppp/2n2n2/8/3NP3/2N5/PPP2PPP/R1BQKB1R b KQkq - 2 9", { SquareE7, SquareE5, Pawn, None } },
#endif

    // Sicilian continuation: Accelerated Dragon variation
    { "r1bqkbnr/pp1ppppp/2n5/8/3NP3/8/PPP2PPP/RNBQKB1R b KQkq - 0 7", { SquareG7, SquareG6, Pawn, None } },

    // Sicilian continuation: Taimanov variation
    { "r1bqkbnr/pp1ppppp/2n5/8/3NP3/8/PPP2PPP/RNBQKB1R b KQkq - 0 7", { SquareE7, SquareE6, Pawn, None } },

    // French (e4 e6)
    { "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", { SquareE7, SquareE6, Pawn, None } },
    { "rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", { SquareD2, SquareD4, Pawn, None } },
    { "rnbqkbnr/pppp1ppp/4p3/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq d3 0 3", { SquareD7, SquareD5, Pawn, None } },

    // French continuation: Tarrasch variation
    { "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq d6 0 4", { SquareB1, SquareD2, Knight, None } },

    // French continuation: Winawer variation
    { "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq d6 0 4", { SquareB1, SquareC3, Knight, None } },
    { "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/2N5/PPP2PPP/R1BQKBNR b KQkq - 1 5", { SquareF8, SquareB4, Bishop, None } },

    // French continuation: Classical variation
    { "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/2N5/PPP2PPP/R1BQKBNR b KQkq - 1 5", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/ppp2ppp/4pn2/3p4/3PP3/2N5/PPP2PPP/R1BQKBNR w KQkq - 2 6", { SquareC1, SquareG5, Bishop, None } },

    // French continuation: Advance variation
    { "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq d6 0 4", { SquareE4, SquareE5, Pawn, None } },

    // Caro-Kann (e4 c6)
    { "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", { SquareC7, SquareC6, Pawn, None } },
    { "rnbqkbnr/pp1ppppp/2p5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", { SquareD2, SquareD4, Pawn, None } },
    { "rnbqkbnr/pp1ppppp/2p5/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq d3 0 3", { SquareD7, SquareD5, Pawn, None } },

    // Caro-Kann continuation: Classical variation
    { "rnbqkbnr/pp2pppp/2p5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq d6 0 4", { SquareB1, SquareC3, Knight, None } },
    { "rnbqkbnr/pp2pppp/2p5/3p4/3PP3/2N5/PPP2PPP/R1BQKBNR b KQkq - 1 5", { SquareD5, SquareE4, Pawn, None } },
    { "rnbqkbnr/pp2pppp/2p5/8/3Pp3/2N5/PPP2PPP/R1BQKBNR w KQkq - 0 6", { SquareC3, SquareE4, Knight, None } },
    { "rnbqkbnr/pp2pppp/2p5/8/3PN3/8/PPP2PPP/R1BQKBNR b KQkq - 0 7", { SquareC8, SquareF5, Bishop, None } },

    // Caro-Kann continuation: Advance variation
    { "rnbqkbnr/pp2pppp/2p5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq d6 0 4", { SquareE4, SquareE5, Pawn, None } },
    { "rnbqkbnr/pp2pppp/2p5/3pP3/3P4/8/PPP2PPP/RNBQKBNR b KQkq - 0 5", { SquareC8, SquareF5, Bishop, None } },

    // Caro-Kann continuation: Exchange variation
    { "rnbqkbnr/pp2pppp/2p5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq d6 0 4", { SquareE4, SquareD5, Pawn, None } },
    { "rnbqkbnr/pp2pppp/2p5/3P4/3P4/8/PPP2PPP/RNBQKBNR b KQkq - 0 5", { SquareC6, SquareD5, Pawn, None } },
    { "rnbqkbnr/pp2pppp/8/3p4/3P4/8/PPP2PPP/RNBQKBNR w KQkq - 0 6", { SquareF1, SquareD3, Bishop, None } },

    // Caro-Kann continuation: Fantasy variation
    { "rnbqkbnr/pp2pppp/2p5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq d6 0 4", { SquareF2, SquareF3, Pawn, None } },

    // Pirc (e4 d6 d4 Nf6)
    // Do not play this opening as black; only as white if black opens it.
    // Therefore, skip 1. e4 d6
    { "rnbqkbnr/ppp1pppp/3p4/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", { SquareD2, SquareD4, Pawn, None } },

    // Pirc continuation: Nc3
    { "rnbqkb1r/ppp1pppp/3p1n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 3", { SquareB1, SquareC3, Knight, None } },

    // Pirc continuation: Bd3
    { "rnbqkb1r/ppp1pppp/3p1n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 3", { SquareF1, SquareD3, Bishop, None } },

    // Scandinavian defense (e4 d5)
    { "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", { SquareD7, SquareD5, Pawn, None } },
    { "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", { SquareE4, SquareD5, Pawn, None } },

    // Scandinavian continuation: Qxd5 variations
    { "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3", { SquareD8, SquareD5, Queen, None } },
    { "rnb1kbnr/ppp1pppp/8/3q4/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 4", { SquareB1, SquareC3, Knight, None } },
    { "rnb1kbnr/ppp1pppp/8/3q4/8/2N5/PPPP1PPP/R1BQKBNR b KQkq - 1 5", { SquareD5, SquareA5, Queen, None } },

    { "rnb1kbnr/ppp1pppp/8/3q4/8/2N5/PPPP1PPP/R1BQKBNR b KQkq - 1 5", { SquareD5, SquareD8, Queen, None } },

    { "rnb1kbnr/ppp1pppp/8/3q4/8/2N5/PPPP1PPP/R1BQKBNR b KQkq - 1 5", { SquareD5, SquareD6, Queen, None } },

    // Scandinavian continuation: Nf6 variations
    { "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/ppp1pppp/5n2/3P4/8/8/PPPP1PPP/RNBQKBNR w KQkq - 1 4", { SquareG1, SquareF3, Knight, None } },

    { "rnbqkb1r/ppp1pppp/5n2/3P4/8/8/PPPP1PPP/RNBQKBNR w KQkq - 1 4", { SquareD2, SquareD4, Pawn, None } },

    // Alekhine's defense (e4 f6)
    // Do not play this opening as black; only as white if black opens it.
    // Therefore, skip 1. e4 f6
    { "rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2", { SquareE4, SquareE5, Pawn, None } },
    { "rnbqkb1r/pppppppp/5n2/4P3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3", { SquareF6, SquareD5, Knight, None } },
    { "rnbqkb1r/pppppppp/8/3nP3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 1 4", { SquareD2, SquareD4, Pawn, None } },
    { "rnbqkb1r/pppppppp/8/3nP3/3P4/8/PPP2PPP/RNBQKBNR b KQkq d3 0 5", { SquareD7, SquareD6, Pawn, None } },

    // Scotch (e4 e5 Nf3 Nc6 d4)
    { "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 4", { SquareD2, SquareD4, Pawn, None } },
    { "r1bqkbnr/pppp1ppp/2n5/4p3/3PP3/5N2/PPP2PPP/RNBQKB1R b KQkq d3 0 5", { SquareE5, SquareD4, Pawn, None } },
    { "r1bqkbnr/pppp1ppp/2n5/8/3pP3/5N2/PPP2PPP/RNBQKB1R w KQkq - 0 6", { SquareF3, SquareD4, Knight, None } },

    // Scotch continuation: Schmidt variation
    { "r1bqkbnr/pppp1ppp/2n5/8/3NP3/8/PPP2PPP/RNBQKB1R b KQkq - 0 7", { SquareG8, SquareF6, Knight, None } },
    { "r1bqkb1r/pppp1ppp/2n2n2/8/3NP3/8/PPP2PPP/RNBQKB1R w KQkq - 1 8", { SquareD4, SquareC6, Knight, None } },
    { "r1bqkb1r/pppp1ppp/2N2n2/8/4P3/8/PPP2PPP/RNBQKB1R b KQkq - 0 9", { SquareB7, SquareC6, Pawn, None } },
    { "r1bqkb1r/p1pp1ppp/2p2n2/8/4P3/8/PPP2PPP/RNBQKB1R w KQkq - 0 10", { SquareE4, SquareE5, Pawn, None } },

    // Scotch continuation: Classical variation with Nxc6
    { "r1bqkbnr/pppp1ppp/2n5/8/3NP3/8/PPP2PPP/RNBQKB1R b KQkq - 0 7", { SquareF8, SquareC5, Bishop, None } },
    { "r1bqk1nr/pppp1ppp/2n5/2b5/3NP3/8/PPP2PPP/RNBQKB1R w KQkq - 1 8", { SquareD4, SquareC6, Knight, None } },
    { "r1bqk1nr/pppp1ppp/2N5/2b5/4P3/8/PPP2PPP/RNBQKB1R b KQkq - 0 9", { SquareB7, SquareC6, Pawn, None } },

    // Scotch continuation: Classical variation with Nb3
    { "r1bqk1nr/pppp1ppp/2n5/2b5/3NP3/8/PPP2PPP/RNBQKB1R w KQkq - 1 8", { SquareD4, SquareB3, Knight, None } },
    { "r1bqk1nr/pppp1ppp/2n5/2b5/4P3/1N6/PPP2PPP/RNBQKB1R b KQkq - 2 9", { SquareC5, SquareB6, Bishop, None } },

    // Vienna (e4 e5 Nc3)
    { "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2", { SquareB1, SquareC3, Knight, None } },

    // Vienna continuation: Vienna gambit
    { "rnbqkbnr/pppp1ppp/8/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b KQkq - 1 3", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/pppp1ppp/5n2/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR w KQkq - 2 4", { SquareF2, SquareF4, Pawn, None } },
    { "rnbqkb1r/pppp1ppp/5n2/4p3/4PP2/2N5/PPPP2PP/R1BQKBNR b KQkq f3 0 5", { SquareD7, SquareD5, Pawn, None } },
    { "rnbqkb1r/ppp2ppp/5n2/3pp3/4PP2/2N5/PPPP2PP/R1BQKBNR w KQkq d6 0 6", { SquareF4, SquareE5, Pawn, None } },
    { "rnbqkb1r/ppp2ppp/5n2/3pP3/4P3/2N5/PPPP2PP/R1BQKBNR b KQkq - 0 7", { SquareF6, SquareE4, Knight, None } },

    // Vienna continuation: Falkbeer, Mieses variation
    { "rnbqkb1r/pppp1ppp/5n2/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR w KQkq - 2 4", { SquareG2, SquareG3, Pawn, None } },

    // Vienna continuation: Max Lange defense
    { "rnbqkbnr/pppp1ppp/8/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b KQkq - 1 3", { SquareB8, SquareC6, Knight, None } },

    // Vienna continuation: Anderssen defense
    { "rnbqkbnr/pppp1ppp/8/4p3/4P3/2N5/PPPP1PPP/R1BQKBNR b KQkq - 1 3", { SquareF8, SquareC5, Bishop, None } },

    // Center game (e4 e5 d4)
    // Do not play this opening as white; only as black if white opens it.
    // Therefore, skip 1. e4 e5 2. d4
    { "rnbqkbnr/pppp1ppp/8/4p3/3PP3/8/PPP2PPP/RNBQKBNR b KQkq d3 0 3", { SquareE5, SquareD4, Pawn, None } },
    { "rnbqkbnr/pppp1ppp/8/8/3pP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 4", { SquareD1, SquareD4, Queen, None } },
    { "rnbqkbnr/pppp1ppp/8/8/3QP3/8/PPP2PPP/RNB1KBNR b KQkq - 0 5", { SquareB8, SquareC6, Knight, None } },

    // King's gambit (e4 e5 f4)
    // Do not play this opening as white; only as black if white opens it.
    // Therefore, skip 1. e4 e5 2. f4
    { "rnbqkbnr/pppp1ppp/8/4p3/4PP2/8/PPPP2PP/RNBQKBNR b KQkq f3 0 3", { SquareE5, SquareF4, Pawn, None } },
    { "rnbqkbnr/pppp1ppp/8/8/4Pp2/8/PPPP2PP/RNBQKBNR w KQkq - 0 4", { SquareG1, SquareF3, Knight, None } },

    // King's gambit continuation: g5
    { "rnbqkbnr/pppp1ppp/8/8/4Pp2/5N2/PPPP2PP/RNBQKB1R b KQkq - 1 5", { SquareG7, SquareG5, Pawn, None } },

    // King's gambit continuation: Fischer defense
    { "rnbqkbnr/pppp1ppp/8/8/4Pp2/5N2/PPPP2PP/RNBQKB1R b KQkq - 1 5", { SquareD7, SquareD6, Pawn, None } },

    // Queen's gambit (d4 d5 c4)
    { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0", { SquareD2, SquareD4, Pawn, None } },
    { "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 1", { SquareD7, SquareD5, Pawn, None } },
    { "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq d6 0 2", { SquareC2, SquareC4, Pawn, None } },

    // Queen's gambit continuation: Albin countergambit
    { "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 3", { SquareE7, SquareE5, Pawn, None } },
    { "rnbqkbnr/ppp2ppp/8/3pp3/2PP4/8/PP2PPPP/RNBQKBNR w KQkq e6 0 4", { SquareD4, SquareE5, Pawn, None } },
    { "rnbqkbnr/ppp2ppp/8/3pP3/2P5/8/PP2PPPP/RNBQKBNR b KQkq - 0 5", { SquareD5, SquareD4, Pawn, None } },
    { "rnbqkbnr/ppp2ppp/8/4P3/2Pp4/8/PP2PPPP/RNBQKBNR w KQkq - 0 6", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkbnr/ppp2ppp/8/4P3/2Pp4/5N2/PP2PPPP/RNBQKB1R b KQkq - 1 7", { SquareB8, SquareC6, Knight, None } },
    { "r1bqkbnr/ppp2ppp/2n5/4P3/2Pp4/5N2/PP2PPPP/RNBQKB1R w KQkq - 2 8", { SquareG2, SquareG3, Pawn, None } },

#if LONG_LINES
    // Queen's gambit continuation: Lasker trap
    // Obviously, don't play into this as white. Only include black's moves.
    //{ "rnbqkbnr/ppp2ppp/8/4P3/2Pp4/8/PP2PPPP/RNBQKBNR w KQkq - 0 6", { SquareE2, SquareE3, Pawn, None } },
    { "rnbqkbnr/ppp2ppp/8/4P3/2Pp4/4P3/PP3PPP/RNBQKBNR b KQkq - 0 7", { SquareF8, SquareB4, Bishop, None } },
    //{ "rnbqk1nr/ppp2ppp/8/4P3/1bPp4/4P3/PP3PPP/RNBQKBNR w KQkq - 1 8", { SquareC1, SquareD2, Bishop, None } },
    { "rnbqk1nr/ppp2ppp/8/4P3/1bPp4/4P3/PP1B1PPP/RN1QKBNR b KQkq - 2 9", { SquareD4, SquareE3, Pawn, None } },
    //{ "rnbqk1nr/ppp2ppp/8/4P3/1bP5/4p3/PP1B1PPP/RN1QKBNR w KQkq - 0 10", { SquareD2, SquareB4, Bishop, None } },
    { "rnbqk1nr/ppp2ppp/8/4P3/1BP5/4p3/PP3PPP/RN1QKBNR b KQkq - 0 11", { SquareE3, SquareF2, Pawn, None } },
    //{ "rnbqk1nr/ppp2ppp/8/4P3/1BP5/8/PP3pPP/RN1QKBNR w KQkq - 0 12", { SquareE1, SquareE2, King, None } },
    { "rnbqk1nr/ppp2ppp/8/4P3/1BP5/8/PP2KpPP/RN1Q1BNR b kq - 1 13", { SquareF2, SquareG1, Pawn, Knight } },
    //{ "rnbqk1nr/ppp2ppp/8/4P3/1BP5/8/PP2K1PP/RN1Q1BnR w kq - 0 14", { SquareH1, SquareG1, Rook, None } },
    { "rnbqk1nr/ppp2ppp/8/4P3/1BP5/8/PP2K1PP/RN1Q1BR1 b kq - 0 15", { SquareC8, SquareG4, Bishop, None } },
#endif

    // Queen's gambit continuation: Queen's gambit declined
    { "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 3", { SquareE7, SquareE6, Pawn, None } },
    { "rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 4", { SquareB1, SquareC3, Knight, None } },
#if LONG_LINES
    { "rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq - 1 5", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 6", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b KQkq - 3 7", { SquareF8, SquareE7, Bishop, None } },
    { "rnbqk2r/ppp1bppp/4pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R w KQkq - 4 8", { SquareC1, SquareG5, Bishop, None } },
    { "rnbqk2r/ppp1bppp/4pn2/3p2B1/2PP4/2N2N2/PP2PPPP/R2QKB1R b KQkq - 5 9", { SquareE8, SquareG8, King, None } },
    { "rnbq1rk1/ppp1bppp/4pn2/3p2B1/2PP4/2N2N2/PP2PPPP/R2QKB1R w KQ - 6 10", { SquareE2, SquareE3, Pawn, None } },
#endif

    // Queen's gambit continuation: Catalan
    { "rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 4", { SquareG1, SquareF3, Knight, None } },
#if LONG_LINES
    { "rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/5N2/PP2PPPP/RNBQKB1R b KQkq - 1 5", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 2 6", { SquareG2, SquareG3, Pawn, None } },
#endif

    // Queen's gambit continuation: Queen's gambit accepted, e4
    { "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 3", { SquareD5, SquareC4, Pawn, None } },
    { "rnbqkbnr/ppp1pppp/8/8/2pP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 4", { SquareE2, SquareE4, Pawn, None } },
    { "rnbqkbnr/ppp1pppp/8/8/2pPP3/8/PP3PPP/RNBQKBNR b KQkq e3 0 5", { SquareE7, SquareE5, Pawn, None } },

    // Queen's gambit continuation: Queen's gambit accepted, Nf3
    { "rnbqkbnr/ppp1pppp/8/8/2pP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 4", { SquareG1, SquareF3, Knight, None } },
#if LONG_LINES
    { "rnbqkbnr/ppp1pppp/8/8/2pP4/5N2/PP2PPPP/RNBQKB1R b KQkq - 1 5", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/ppp1pppp/5n2/8/2pP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 2 6", { SquareE2, SquareE3, Pawn, None } },
    { "rnbqkb1r/ppp1pppp/5n2/8/2pP4/4PN2/PP3PPP/RNBQKB1R b KQkq - 0 7", { SquareE7, SquareE6, Pawn, None } },
    { "rnbqkb1r/ppp2ppp/4pn2/8/2pP4/4PN2/PP3PPP/RNBQKB1R w KQkq - 0 8", { SquareF1, SquareC4, Bishop, None } },
#endif

    // Queen's gambit continuation: Slav defense
    { "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 3", { SquareC7, SquareC6, Pawn, None } },
    { "rnbqkbnr/pp2pppp/2p5/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 4", { SquareB1, SquareC3, Knight, None } },
    { "rnbqkbnr/pp2pppp/2p5/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq - 1 5", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/pp2pppp/2p2n2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 6", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkb1r/pp2pppp/2p2n2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b KQkq - 3 7", { SquareD5, SquareC4, Pawn, None } },
    { "rnbqkb1r/pp2pppp/2p2n2/8/2pP4/2N2N2/PP2PPPP/R1BQKB1R w KQkq - 0 8", { SquareA2, SquareA4, Pawn, None } },

    // Queen's gambit continuation: Semi-Slav defense
    { "rnbqkb1r/pp2pppp/2p2n2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b KQkq - 3 7", { SquareE7, SquareE6, Pawn, None } },

    // Queen's gambit continuation: Slav defense, quiet variation
    { "rnbqkbnr/pp2pppp/2p5/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 4", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkbnr/pp2pppp/2p5/3p4/2PP4/5N2/PP2PPPP/RNBQKB1R b KQkq - 1 5", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/pp2pppp/2p2n2/3p4/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 2 6", { SquareE2, SquareE3, Pawn, None } },

    // Catalan (d4 Nf6 c4 e6 g3)
    { "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 1", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 1 2", { SquareC2, SquareC4, Pawn, None } },
    { "rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 3", { SquareE7, SquareE6, Pawn, None } },
    { "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 4", { SquareG2, SquareG3, Pawn, None } },
    { "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/6P1/PP2PP1P/RNBQKBNR b KQkq - 0 5", { SquareD7, SquareD5, Pawn, None } },
    { "rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/6P1/PP2PP1P/RNBQKBNR w KQkq d6 0 6", { SquareG1, SquareF3, Knight, None } },

    // King's Indian defense (d4 Nf6 c4 g6)
    { "rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 3", { SquareG7, SquareG6, Pawn, None } },
    { "rnbqkb1r/pppppp1p/5np1/8/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 4", { SquareB1, SquareC3, Knight, None } },
    { "rnbqkb1r/pppppp1p/5np1/8/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq - 1 5", { SquareF8, SquareG7, Bishop, None } },
    { "rnbqk2r/ppppppbp/5np1/8/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 6", { SquareE2, SquareE4, Pawn, None } },
    { "rnbqk2r/ppppppbp/5np1/8/2PPP3/2N5/PP3PPP/R1BQKBNR b KQkq e3 0 7", { SquareD7, SquareD6, Pawn, None } },

    // King's Indian defense continuation: Main line
    { "rnbqk2r/ppp1ppbp/3p1np1/8/2PPP3/2N5/PP3PPP/R1BQKBNR w KQkq - 0 8", { SquareG1, SquareF3, Knight, None } },
#if LONG_LINES
    { "rnbqk2r/ppp1ppbp/3p1np1/8/2PPP3/2N2N2/PP3PPP/R1BQKB1R b KQkq - 1 9", { SquareE8, SquareG8, King, None } },
    { "rnbq1rk1/ppp1ppbp/3p1np1/8/2PPP3/2N2N2/PP3PPP/R1BQKB1R w KQ - 2 10", { SquareF1, SquareE2, Bishop, None } },
    { "rnbq1rk1/ppp1ppbp/3p1np1/8/2PPP3/2N2N2/PP2BPPP/R1BQK2R b KQ - 3 11", { SquareE7, SquareE5, Pawn, None } },
    { "rnbq1rk1/ppp2pbp/3p1np1/4p3/2PPP3/2N2N2/PP2BPPP/R1BQK2R w KQ e6 0 12", { SquareE1, SquareG1, King, None } },
    { "rnbq1rk1/ppp2pbp/3p1np1/4p3/2PPP3/2N2N2/PP2BPPP/R1BQ1RK1 b - - 1 13", { SquareB8, SquareC6, Knight, None } },
#endif
    
    // King's Indian defense continuation: Samisch variation
    { "rnbqk2r/ppp1ppbp/3p1np1/8/2PPP3/2N5/PP3PPP/R1BQKBNR w KQkq - 0 8", { SquareF2, SquareF3, Pawn, None } },
    
    // King's Indian defense continuation: Makogonov variation
    { "rnbqk2r/ppp1ppbp/3p1np1/8/2PPP3/2N5/PP3PPP/R1BQKBNR w KQkq - 0 8", { SquareH2, SquareH3, Pawn, None } },
    
    // Nimzo-Indian defense (d4 Nf6 c4 e6 Nc3 Bb2)
    { "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 4", { SquareB1, SquareC3, Knight, None } },
    { "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq - 1 5", { SquareF8, SquareB4, Bishop, None } },

    // Nimzo-Indian defense continuation: Rubenstein variation
    { "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 6", { SquareE2, SquareE3, Pawn, None } },
#if LONG_LINES
    { "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N1P3/PP3PPP/R1BQKBNR b KQkq - 0 7", { SquareE8, SquareG8, King, None } },
    { "rnbq1rk1/pppp1ppp/4pn2/8/1bPP4/2N1P3/PP3PPP/R1BQKBNR w KQ - 1 8", { SquareG1, SquareF3, Knight, None } },
    { "rnbq1rk1/pppp1ppp/4pn2/8/1bPP4/2N1PN2/PP3PPP/R1BQKB1R b KQ - 2 9", { SquareD7, SquareD5, Pawn, None } },
    { "rnbq1rk1/ppp2ppp/4pn2/3p4/1bPP4/2N1PN2/PP3PPP/R1BQKB1R w KQ d6 0 10", { SquareF1, SquareD3, Bishop, None } },
    { "rnbq1rk1/ppp2ppp/4pn2/3p4/1bPP4/2NBPN2/PP3PPP/R1BQK2R b KQ - 1 11", { SquareC7, SquareC5, Pawn, None } },
    { "rnbq1rk1/pp3ppp/4pn2/2pp4/1bPP4/2NBPN2/PP3PPP/R1BQK2R w KQ c6 0 12", { SquareE1, SquareG1, King, None } },
    { "rnbq1rk1/pp3ppp/4pn2/2pp4/1bPP4/2NBPN2/PP3PPP/R1BQ1RK1 b - - 1 13", { SquareB8, SquareC6, Knight, None } },
    { "r1bq1rk1/pp3ppp/2n1pn2/2pp4/1bPP4/2NBPN2/PP3PPP/R1BQ1RK1 w - - 2 14", { SquareA2, SquareA3, Pawn, None } },
    { "r1bq1rk1/pp3ppp/2n1pn2/2pp4/1bPP4/P1NBPN2/1P3PPP/R1BQ1RK1 b - - 0 15", { SquareB4, SquareC3, Bishop, None } },
    { "r1bq1rk1/pp3ppp/2n1pn2/2pp4/2PP4/P1bBPN2/1P3PPP/R1BQ1RK1 w - - 0 16", { SquareB2, SquareC3, Pawn, None } },
    { "r1bq1rk1/pp3ppp/2n1pn2/2pp4/2PP4/P1PBPN2/5PPP/R1BQ1RK1 b - - 0 17", { SquareD5, SquareC4, Pawn, None } },
    { "r1bq1rk1/pp3ppp/2n1pn2/2p5/2pP4/P1PBPN2/5PPP/R1BQ1RK1 w - - 0 18", { SquareD3, SquareC4, Bishop, None } },
    { "r1bq1rk1/pp3ppp/2n1pn2/2p5/2BP4/P1P1PN2/5PPP/R1BQ1RK1 b - - 0 19", { SquareD8, SquareC7, Queen, None } },
#endif

    // Nimzo-Indian defense continuation: Classical variation
    { "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 6", { SquareD1, SquareC2, Queen, None } },
#if LONG_LINES
    { "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N5/PPQ1PPPP/R1B1KBNR b KQkq - 3 7", { SquareB4, SquareC3, Bishop, None} },
    { "rnbqk2r/pppp1ppp/4pn2/8/2PP4/2b5/PPQ1PPPP/R1B1KBNR w KQkq - 0 8", { SquareC2, SquareC3, Queen, None } },
#endif

    // Nimzo-Indian defense continuation: Three knights variation
    { "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 6", { SquareG1, SquareF3, Knight, None } },

    // Nimzo-Indian defense continuation: Samisch variation
    { "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 6", { SquareA2, SquareA3, Pawn, None } },
#if LONG_LINES
    { "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/P1N5/1P2PPPP/R1BQKBNR b KQkq - 0 7", { SquareB4, SquareC3, Bishop, None } },
    { "rnbqk2r/pppp1ppp/4pn2/8/2PP4/P1b5/1P2PPPP/R1BQKBNR w KQkq - 0 8", { SquareB2, SquareC3, Pawn, None } },
#endif

    // Queen's Indian defense (d4 Nf6 c4 e6 Nf3 b6)
    { "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 4", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/5N2/PP2PPPP/RNBQKB1R b KQkq - 1 5", { SquareB7, SquareB6, Pawn, None } },

    // Queen's Indian defense continuation: Fianchetto Nimzowitsch variation
    { "rnbqkb1r/p1pp1ppp/1p2pn2/8/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 0 6", { SquareG2, SquareG3, Pawn, None } },
    { "rnbqkb1r/p1pp1ppp/1p2pn2/8/2PP4/5NP1/PP2PP1P/RNBQKB1R b KQkq - 0 7", { SquareC8, SquareA6, Bishop, None } },

    // Queen's Indian defense continuation: Fianchetto traditional line
    { "rnbqkb1r/p1pp1ppp/1p2pn2/8/2PP4/5NP1/PP2PP1P/RNBQKB1R b KQkq - 0 7", { SquareC8, SquareB7, Bishop, None } },

    // Queen's Indian defense continuation: Petrosian variation
    { "rnbqkb1r/p1pp1ppp/1p2pn2/8/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 0 6", { SquareA2, SquareA3, Pawn, None } },

    // Queen's Indian defense continuation: Kasparov variation
    { "rnbqkb1r/p1pp1ppp/1p2pn2/8/2PP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 0 6", { SquareB1, SquareC3, Knight, None } },

    // Bogo Indian defense (d4 Nf6 c4 e6 Nf3 Bb4+)
    { "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/5N2/PP2PPPP/RNBQKB1R b KQkq - 1 5", { SquareF8, SquareB4, Bishop, None } },

    // Bogo Indian defense continuation: Main line
    { "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 2 6", { SquareC1, SquareD2, Bishop, None } },

    // Bogo Indian defense continuation: Grunfeld variation
    { "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 2 6", { SquareB1, SquareD2, Knight, None } },
    
    // Bogo Indian defense continuation: Transposition to Nimzo-Indian defense, three knights variation
    { "rnbqk2r/pppp1ppp/4pn2/8/1bPP4/5N2/PP2PPPP/RNBQKB1R w KQkq - 2 6", { SquareB1, SquareC3, Knight, None } },

    // Grunfeld defense (d4 Nf6 c4 g6 Nc3 d5)
    { "rnbqkb1r/pppppp1p/5np1/8/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq - 1 5", { SquareD7, SquareD5, Pawn, None } },

    // Grunfeld defense continuation: Exchange variation
    { "rnbqkb1r/ppp1pp1p/5np1/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq d6 0 6", { SquareC4, SquareD5, Pawn, None } },
#if LONG_LINES
    { "rnbqkb1r/ppp1pp1p/5np1/3P4/3P4/2N5/PP2PPPP/R1BQKBNR b KQkq - 0 7", { SquareF6, SquareD5, Knight, None } },
    { "rnbqkb1r/ppp1pp1p/6p1/3n4/3P4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 8", { SquareE2, SquareE4, Pawn, None } },
    { "rnbqkb1r/ppp1pp1p/6p1/3n4/3PP3/2N5/PP3PPP/R1BQKBNR b KQkq e3 0 9", { SquareD5, SquareC3, Knight, None } },
    { "rnbqkb1r/ppp1pp1p/6p1/8/3PP3/2n5/PP3PPP/R1BQKBNR w KQkq - 0 10", { SquareB2, SquareC3, Pawn, None } },
#endif

    // Grunfeld defense continuation: Russian variation
    { "rnbqkb1r/ppp1pp1p/5np1/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq d6 0 6", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkb1r/ppp1pp1p/5np1/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b KQkq - 1 7", { SquareF8, SquareG7, Bishop, None } },
    { "rnbqk2r/ppp1ppbp/5np1/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R w KQkq - 2 8", { SquareD1, SquareB3, Queen, None } },
#if LONG_LINES
    { "rnbqk2r/ppp1ppbp/5np1/3p4/2PP4/1QN2N2/PP2PPPP/R1B1KB1R b KQkq - 3 9", { SquareD5, SquareC4, Pawn, None } },
    { "rnbqk2r/ppp1ppbp/5np1/8/2pP4/1QN2N2/PP2PPPP/R1B1KB1R w KQkq - 0 10", { SquareB3, SquareC4, Queen, None } },
#endif

    // Grunfeld defense continuation: Petrosian variation
    { "rnbqk2r/ppp1ppbp/5np1/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R w KQkq - 2 8", { SquareC1, SquareG5, Bishop, None } },
#if LONG_LINES
    { "rnbqk2r/ppp1ppbp/5np1/3p2B1/2PP4/2N2N2/PP2PPPP/R2QKB1R b KQkq - 3 9", { SquareF6, SquareE4, Knight, None } },
    { "rnbqk2r/ppp1ppbp/6p1/3p2B1/2PPn3/2N2N2/PP2PPPP/R2QKB1R w KQkq - 4 10", { SquareC4, SquareD5, Pawn, None } },
    { "rnbqk2r/ppp1ppbp/6p1/3P2B1/3Pn3/2N2N2/PP2PPPP/R2QKB1R b KQkq - 0 11", { SquareE4, SquareG5, Knight, None } },
    { "rnbqk2r/ppp1ppbp/6p1/3P2n1/3P4/2N2N2/PP2PPPP/R2QKB1R w KQkq - 0 12", { SquareF3, SquareG5, Knight, None } },
    { "rnbqk2r/ppp1ppbp/6p1/3P2N1/3P4/2N5/PP2PPPP/R2QKB1R b KQkq - 0 13", { SquareE7, SquareE6, Pawn, None } },
#endif

    // Dutch (d4 f5)
    { "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 1", { SquareF7, SquareF5, Pawn, None } },

    // Dutch (d4 e6)
    { "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 1", { SquareE7, SquareE6, Pawn, None } },
    { "rnbqkbnr/pppp1ppp/4p3/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2", { SquareC2, SquareC4, Pawn, None } },
    { "rnbqkbnr/pppp1ppp/4p3/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 3", { SquareF7, SquareF5, Pawn, None } },

    // Dutch continuation: Leningrad variation
    { "rnbqkbnr/ppppp1pp/8/5p2/3P4/8/PPP1PPPP/RNBQKBNR w KQkq f6 0 2", { SquareG2, SquareG3, Pawn, None } },
    { "rnbqkbnr/ppppp1pp/8/5p2/3P4/6P1/PPP1PP1P/RNBQKBNR b KQkq - 0 3", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/ppppp1pp/5n2/5p2/3P4/6P1/PPP1PP1P/RNBQKBNR w KQkq - 1 4", { SquareF1, SquareG2, Bishop, None } },
    { "rnbqkb1r/ppppp1pp/5n2/5p2/3P4/6P1/PPP1PPBP/RNBQK1NR b KQkq - 2 5", { SquareG7, SquareG6, Pawn, None } },
#if LONG_LINES
    { "rnbqkb1r/ppppp2p/5np1/5p2/3P4/6P1/PPP1PPBP/RNBQK1NR w KQkq - 0 6", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkb1r/ppppp2p/5np1/5p2/3P4/5NP1/PPP1PPBP/RNBQK2R b KQkq - 1 7", { SquareF8, SquareG7, Bishop, None } },
    { "rnbqk2r/ppppp1bp/5np1/5p2/3P4/5NP1/PPP1PPBP/RNBQK2R w KQkq - 2 8", { SquareE1, SquareG1, King, None } },
    { "rnbqk2r/ppppp1bp/5np1/5p2/3P4/5NP1/PPP1PPBP/RNBQ1RK1 b kq - 3 9", { SquareE8, SquareG8, King, None } },
    { "rnbq1rk1/ppppp1bp/5np1/5p2/3P4/5NP1/PPP1PPBP/RNBQ1RK1 w - - 4 10", { SquareC2, SquareC4, Pawn, None } },
    { "rnbq1rk1/ppppp1bp/5np1/5p2/2PP4/5NP1/PP2PPBP/RNBQ1RK1 b - c3 0 11", { SquareD7, SquareD6, Pawn, None } },
#endif

    // Dutch continuation: Classical variation
    { "rnbqkb1r/ppppp1pp/5n2/5p2/3P4/6P1/PPP1PP1P/RNBQKBNR w KQkq - 1 4", { SquareG1, SquareF3, Knight, None } },
#if LONG_LINES
    { "rnbqkb1r/ppppp1pp/5n2/5p2/3P4/5NP1/PPP1PP1P/RNBQKB1R b KQkq - 2 5", { SquareE7, SquareE6, Pawn, None } },
    { "rnbqkb1r/pppp2pp/4pn2/5p2/3P4/5NP1/PPP1PP1P/RNBQKB1R w KQkq - 0 6", { SquareC2, SquareC4, Pawn, None } },
    { "rnbqkb1r/pppp2pp/4pn2/5p2/2PP4/5NP1/PP2PP1P/RNBQKB1R b KQkq c3 0 7", { SquareF8, SquareE7, Bishop, None } },
#endif

    // Dutch continuation: Stonewall
    { "rnbqkb1r/ppppp1pp/5n2/5p2/3P4/6P1/PPP1PPBP/RNBQK1NR b KQkq - 2 5", { SquareE7, SquareE6, Pawn, None } },
#if LONG_LINES
    { "rnbqkb1r/pppp2pp/4pn2/5p2/3P4/6P1/PPP1PPBP/RNBQK1NR w KQkq - 0 6", { SquareC2, SquareC4, Pawn, None } },
    { "rnbqkb1r/pppp2pp/4pn2/5p2/2PP4/6P1/PP2PPBP/RNBQK1NR b KQkq c3 0 7", { SquareD7, SquareD5, Pawn, None } },
    { "rnbqkb1r/ppp3pp/4pn2/3p1p2/2PP4/6P1/PP2PPBP/RNBQK1NR w KQkq d6 0 8", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkb1r/ppp3pp/4pn2/3p1p2/2PP4/5NP1/PP2PPBP/RNBQK2R b KQkq - 1 9", { SquareC7, SquareC6, Pawn, None } },
#endif

    // Dutch continuation: Transposition to French
    { "rnbqkbnr/pppp1ppp/4p3/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2", { SquareE2, SquareE4, Pawn, None } },
    { "rnbqkbnr/pppp1ppp/4p3/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq e3 0 3", { SquareD7, SquareD5, Pawn, None } },

    // Tromposky attack (d4 Nf6 Bg5)
    { "rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 1 2", { SquareC1, SquareG5, Bishop, None } },

    // Tromposky attack continuation: Classical variation
    { "rnbqkb1r/pppppppp/5n2/6B1/3P4/8/PPP1PPPP/RN1QKBNR b KQkq - 2 3", { SquareE7, SquareE5, Pawn, None } },

    // Tromposky attack continuation: d5
    { "rnbqkb1r/pppppppp/5n2/6B1/3P4/8/PPP1PPPP/RN1QKBNR b KQkq - 2 3", { SquareD7, SquareD5, Pawn, None } },

    // Tromposky attack continuation: c5
    { "rnbqkb1r/pppppppp/5n2/6B1/3P4/8/PPP1PPPP/RN1QKBNR b KQkq - 2 3", { SquareC7, SquareC5, Pawn, None } },

    // Benoni defense (d4 Nf6 c4 c5)
    { "rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 3", { SquareC7, SquareC5, Pawn, None } },

    // Benoni defense continuation: Symmetrical English transposition
    { "rnbqkb1r/pp1ppppp/5n2/2p5/2PP4/8/PP2PPPP/RNBQKBNR w KQkq c6 0 4", { SquareG1, SquareF3, Knight, None } },

    // Benoni defense continuation: Benko gambit
    { "rnbqkb1r/pp1ppppp/5n2/2p5/2PP4/8/PP2PPPP/RNBQKBNR w KQkq c6 0 4", { SquareD4, SquareD5, Pawn, None } },
    { "rnbqkb1r/pp1ppppp/5n2/2pP4/2P5/8/PP2PPPP/RNBQKBNR b KQkq - 0 5", { SquareB7, SquareB5, Pawn, None } },

    // Benoni defense continuation: Benko gambit, main line
    { "rnbqkb1r/p2ppppp/5n2/1ppP4/2P5/8/PP2PPPP/RNBQKBNR w KQkq b6 0 6", { SquareG1, SquareF3, Knight, None } },

    // Benoni defense continuation: Benko gambit, half-accepted
    { "rnbqkb1r/p2ppppp/5n2/1ppP4/2P5/8/PP2PPPP/RNBQKBNR w KQkq b6 0 6", { SquareC4, SquareB5, Pawn, None } },

    // Benoni defense continuation: Benko gambit, Qc2
    { "rnbqkb1r/p2ppppp/5n2/1ppP4/2P5/8/PP2PPPP/RNBQKBNR w KQkq b6 0 6", { SquareD1, SquareC2, Queen, None } },

    // Benoni defense continuation: Benko gambit, quiet line
    { "rnbqkb1r/p2ppppp/5n2/1ppP4/2P5/8/PP2PPPP/RNBQKBNR w KQkq b6 0 6", { SquareB1, SquareD2, Knight, None } },

    // London (d4 d5 Nf3 Nf6 Bf4) or (d4 d5 Bf4) or (d4 Nf6 Bf4)
    { "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq d6 0 2", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkbnr/ppp1pppp/8/3p4/3P4/5N2/PPP1PPPP/RNBQKB1R b KQkq - 1 3", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/ppp1pppp/5n2/3p4/3P4/5N2/PPP1PPPP/RNBQKB1R w KQkq - 2 4", { SquareC1, SquareF4, Bishop, None } },

    { "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq d6 0 2", { SquareC1, SquareF4, Bishop, None } },

    { "rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 1 2", { SquareC1, SquareF4, Bishop, None } },

    // London: Indian defense
    { "rnbqkb1r/pppppppp/5n2/8/3P1B2/8/PPP1PPPP/RN1QKBNR b KQkq - 2 3", { SquareG7, SquareG6, Pawn, None } },
    { "rnbqkb1r/pppppp1p/5np1/8/3P1B2/8/PPP1PPPP/RN1QKBNR w KQkq - 0 4", { SquareB1, SquareC3, Knight, None } },

    // King's Indian attack (Nf3 d5 g3)
    { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 1", { SquareD7, SquareD5, Pawn, None } },
    { "rnbqkbnr/ppp1pppp/8/3p4/8/5N2/PPPPPPPP/RNBQKB1R w KQkq d6 0 2", { SquareG2, SquareG3, Pawn, None } },

    // English
    { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0", { SquareC2, SquareC4, Pawn, None } },

    // English continuation: Reversed Sicilian
    { "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq c3 0 1", { SquareE7, SquareE5, Pawn, None } },

    // English continuation: Four knights variation
    { "rnbqkbnr/pppp1ppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR w KQkq e6 0 2", { SquareB1, SquareC3, Knight, None } },
    { "rnbqkbnr/pppp1ppp/8/4p3/2P5/2N5/PP1PPPPP/R1BQKBNR b KQkq - 1 3", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/pppp1ppp/5n2/4p3/2P5/2N5/PP1PPPPP/R1BQKBNR w KQkq - 2 4", { SquareG1, SquareF3, Knight, None } },
    { "rnbqkb1r/pppp1ppp/5n2/4p3/2P5/2N2N2/PP1PPPPP/R1BQKB1R b KQkq - 3 5", { SquareB8, SquareC6, Knight, None } },

    // English continuation: Symmetrical variation
    { "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq c3 0 1", { SquareC7, SquareC5, Pawn, None } },

    // English continuation: Botvinnik system
    { "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq c3 0 1", { SquareG8, SquareF6, Knight, None } },
    { "rnbqkb1r/pppppppp/5n2/8/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 1 2", { SquareB1, SquareC3, Knight, None } },
    { "rnbqkb1r/pppppppp/5n2/8/2P5/2N5/PP1PPPPP/R1BQKBNR b KQkq - 2 3", { SquareG7, SquareG6, Pawn, None } },
    { "rnbqkb1r/pppppp1p/5np1/8/2P5/2N5/PP1PPPPP/R1BQKBNR w KQkq - 0 4", { SquareE2, SquareE4, Pawn, None } },
    { "rnbqkb1r/pppppp1p/5np1/8/2P1P3/2N5/PP1P1PPP/R1BQKBNR b KQkq e3 0 5", { SquareF8, SquareG7, Bishop, None } },
    { "rnbqk2r/ppppppbp/5np1/8/2P1P3/2N5/PP1P1PPP/R1BQKBNR w KQkq - 1 6", { SquareG2, SquareG3, Pawn, None } },
};

static int CompareFunc(const void * a, const void * b)
{
    uint64_t hashA = ((const FENOpening *) a)->hash;
    uint64_t hashB = ((const FENOpening *) b)->hash;
    if (hashA > hashB)
        return 1;
    else if (hashA < hashB)
        return -1;
    else
        return 0;
}

#define SWITCH_RET_STR(x_) case x_: return #x_;

static const char * SquareToString(Square square)
{
    // Not terribly elegant, but fast and functional.
    switch (square)
    {
    SWITCH_RET_STR(SquareA1)
    SWITCH_RET_STR(SquareB1)
    SWITCH_RET_STR(SquareC1)
    SWITCH_RET_STR(SquareD1)
    SWITCH_RET_STR(SquareE1)
    SWITCH_RET_STR(SquareF1)
    SWITCH_RET_STR(SquareG1)
    SWITCH_RET_STR(SquareH1)
    SWITCH_RET_STR(SquareA2)
    SWITCH_RET_STR(SquareB2)
    SWITCH_RET_STR(SquareC2)
    SWITCH_RET_STR(SquareD2)
    SWITCH_RET_STR(SquareE2)
    SWITCH_RET_STR(SquareF2)
    SWITCH_RET_STR(SquareG2)
    SWITCH_RET_STR(SquareH2)
    SWITCH_RET_STR(SquareA3)
    SWITCH_RET_STR(SquareB3)
    SWITCH_RET_STR(SquareC3)
    SWITCH_RET_STR(SquareD3)
    SWITCH_RET_STR(SquareE3)
    SWITCH_RET_STR(SquareF3)
    SWITCH_RET_STR(SquareG3)
    SWITCH_RET_STR(SquareH3)
    SWITCH_RET_STR(SquareA4)
    SWITCH_RET_STR(SquareB4)
    SWITCH_RET_STR(SquareC4)
    SWITCH_RET_STR(SquareD4)
    SWITCH_RET_STR(SquareE4)
    SWITCH_RET_STR(SquareF4)
    SWITCH_RET_STR(SquareG4)
    SWITCH_RET_STR(SquareH4)
    SWITCH_RET_STR(SquareA5)
    SWITCH_RET_STR(SquareB5)
    SWITCH_RET_STR(SquareC5)
    SWITCH_RET_STR(SquareD5)
    SWITCH_RET_STR(SquareE5)
    SWITCH_RET_STR(SquareF5)
    SWITCH_RET_STR(SquareG5)
    SWITCH_RET_STR(SquareH5)
    SWITCH_RET_STR(SquareA6)
    SWITCH_RET_STR(SquareB6)
    SWITCH_RET_STR(SquareC6)
    SWITCH_RET_STR(SquareD6)
    SWITCH_RET_STR(SquareE6)
    SWITCH_RET_STR(SquareF6)
    SWITCH_RET_STR(SquareG6)
    SWITCH_RET_STR(SquareH6)
    SWITCH_RET_STR(SquareA7)
    SWITCH_RET_STR(SquareB7)
    SWITCH_RET_STR(SquareC7)
    SWITCH_RET_STR(SquareD7)
    SWITCH_RET_STR(SquareE7)
    SWITCH_RET_STR(SquareF7)
    SWITCH_RET_STR(SquareG7)
    SWITCH_RET_STR(SquareH7)
    SWITCH_RET_STR(SquareA8)
    SWITCH_RET_STR(SquareB8)
    SWITCH_RET_STR(SquareC8)
    SWITCH_RET_STR(SquareD8)
    SWITCH_RET_STR(SquareE8)
    SWITCH_RET_STR(SquareF8)
    SWITCH_RET_STR(SquareG8)
    SWITCH_RET_STR(SquareH8)
    }
    return "";
}

static const char * PieceToString(PieceType piece)
{
    switch (piece)
    {
    SWITCH_RET_STR(None)
    SWITCH_RET_STR(Pawn)
    SWITCH_RET_STR(Knight)
    SWITCH_RET_STR(Bishop)
    SWITCH_RET_STR(Rook)
    SWITCH_RET_STR(Queen)
    SWITCH_RET_STR(King)
    }
    return "";
}

int main(int argc, char ** argv)
{
    int result = Init(NULL);
    if (result != 0)
        return result;

    ZobristGenerate();

    const size_t numOpenings = sizeof(s_openingBook) / sizeof(s_openingBook[0]);
    Move moves[222];
    int maxPly = 0;

    for (size_t i = 0; i < numOpenings; ++i)
    {
        FENOpening * opening = &s_openingBook[i];

        Board board;
        if (!ParseFEN(opening->fen, &board))
        {
            printf("Invalid FEN: %s\n", opening->fen);
            return 1;
        }

        opening->hash = board.hash;

        if (board.ply > maxPly)
            maxPly = board.ply;

        // Simple checks to catch copy-paste errors...
        PieceType piece;
        Player playerOfPiece;
        BoardGetPlayerPieceAtSquare(&board, opening->nextMove.from, &piece, &playerOfPiece);
        if (piece != opening->nextMove.piece)
        {
            printf("Invalid piece (FEN: %s)\n", opening->fen);
            return 1;
        }
        if (playerOfPiece != board.playerToMove)
        {
            printf("Invalid player (FEN: %s)\n", opening->fen);
            return 1;
        }
        uint8_t numMoves = GetValidMoves(&board, moves);
        bool isValid = false;
        for (size_t j = 0; j < numMoves; j++)
        {
            if (MoveEquals(moves[j], opening->nextMove))
            {
                isValid = true;
                break;
            }
        }
        if (!isValid)
        {
            printf("Invalid move (FEN: %s)\n", opening->fen);
            return 1;
        }
    }

    // Verification to make sure there are no repeats.
    // This is a slow double iteration but it's way easier to write the code this way...
    for (size_t i = 0; i < numOpenings; ++i)
    {
        for (size_t j = i + 1; j < numOpenings; ++j)
        {
            FENOpening * a = &s_openingBook[i];
            FENOpening * b = &s_openingBook[j];
            if (a->hash == b->hash && MoveEquals(a->nextMove, b->nextMove))
            {
                printf("Duplicate position/move: %s\n", a->fen);
                return 1;
            }
        }
    }

    qsort(s_openingBook, numOpenings, sizeof(s_openingBook[0]), &CompareFunc);

    FILE * of = fopen("OpeningBookGenerated.c", "w");
    if (of == NULL)
        return 1;

    fprintf(of, "#define NUM_OPENINGS %i\n", (int) numOpenings);
    fprintf(of, "#define MAX_OPENING_BOOK_PLY %i\n\n", (int) maxPly);
    fprintf(of, "static const Opening s_openingBook[NUM_OPENINGS] =\n{\n");

    for (size_t i = 0; i < numOpenings; ++i)
    {
        FENOpening * opening = &s_openingBook[i];

        fprintf(of, "    { 0x%016" PRIx64 ", { %s, %s, %s, %s } }",
                opening->hash,
                SquareToString(opening->nextMove.from),
                SquareToString(opening->nextMove.to),
                PieceToString(opening->nextMove.piece),
                PieceToString(opening->nextMove.promotion));

        if (i < numOpenings - 1)
            fprintf(of, ",\n");
        else
            fprintf(of, "\n");
    }

    fprintf(of, "};\n");

    fclose(of);

    Cleanup();

    return 0;
}
