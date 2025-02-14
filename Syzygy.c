#include "Board.h"
#include "Evaluation.h"
#include "FEN.h"
#include "MemoryMappedFile.h"
#include "MinMax.h"
#include "MoveGeneration.h"
#include "Mutex.h"
#include "Intrinsics.h"
#include "Sort.h"
#include "StaticAssert.h"
#include "StringStruct.h"
#include "Syzygy.h"
#include "WindowsInclude.h"
#include "Zobrist.h"

#include <stdendian.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int MaxCardinality = 0;

// Max number of supported pieces
#define TBPIECES 7

// Max DTZ supported, large enough to deal with the syzygy TB limit.
#define MAX_DTZ (1 << 18)

#define PAWN_VALUE_EG 2080

typedef enum TBType
{
    TBTypeWDL, // Win/Draw/Loss
    TBTypeDTZ  // Distance-to-zero
} TBType;

// Each table has a set of flags: all of them refer to DTZ tables, the last one to WDL tables
typedef enum TBFlag
{
    TBFlagSTM = 1,
    TBFlagMapped = 2,
    TBFlagWinPlies = 4,
    TBFlagLossPlies = 8,
    TBFlagWide = 16,
    TBFlagSingleValue = 128
} TBFlag;

typedef enum TBPiece
{
    TBPieceNone,
    TBPieceWPawn = Pawn,
    TBPieceWKnight,
    TBPieceWBishop,
    TBPieceWRook,
    TBPieceWQueen,
    TBPieceWKing,
    TBPieceBPawn = Pawn + 8,
    TBPieceBKnight,
    TBPieceBBishop,
    TBPieceBRook,
    TBPieceBQueen,
    TBPieceBKing
} TBPiece;

static const char * PieceToChar = " PNBRQK  pnbrqk";

int MapPawns[NUM_SQUARES];
int MapB1H1H7[NUM_SQUARES];
int MapA1D1D4[NUM_SQUARES];
int MapKK[10][NUM_SQUARES]; // [MapA1D1D4][NUM_SQUARES]

int Binomial[6][NUM_SQUARES];    // [k][n] k elements from a set of n elements
int LeadPawnIdx[6][NUM_SQUARES]; // [leadPawnsCnt][NUM_SQUARES]
int LeadPawnsSize[6][4];       // [leadPawnsCnt][FileA..FileD]

// Comparison function to sort leading pawns in ascending MapPawns[] order
static bool pawns_comp(void * i, void * j)
{
    Square a = *((Square *) i);
    Square b = *((Square *) j);
    return MapPawns[a] < MapPawns[b];
}

static bool square_comp(void * i, void * j)
{
    Square a = *((Square *) i);
    Square b = *((Square *) j);
    return a < b;
}

static int off_A1H8(Square sq)
{
    return (int)SquareGetRank(sq) - (int)SquareGetFile(sq);
}

static const int32_t WDL_to_value[] =
{
    CHECKMATE_LOSE, // WDLLoss
    -2,             // WDLBlessedLoss
    0,              // WDLDraw
    2,              // WDLCursedWin
    CHECKMATE_WIN   // WDLWin
};

static uint8_t ReadU8(const void * addr)
{
    uint8_t v;
    memcpy(&v, addr, sizeof(uint8_t));
    return v;
}

static uint16_t ReadU16LE(const void * addr)
{
    uint16_t v;
    memcpy(&v, addr, sizeof(uint16_t));
#if _BYTE_ORDER == _BIG_ENDIAN
    intrinsic_bswap16(v);
#endif
    return v;
}

static uint32_t ReadU32LE(const void * addr)
{
    uint32_t v;
    memcpy(&v, addr, sizeof(uint32_t));
#if _BYTE_ORDER == _BIG_ENDIAN
    intrinsic_bswap32(v);
#endif
    return v;
}

static uint64_t ReadU64LE(const void * addr)
{
    uint64_t v;
    memcpy(&v, addr, sizeof(uint64_t));
#if _BYTE_ORDER == _BIG_ENDIAN
    intrinsic_bswap64(v);
#endif
    return v;
}

static uint16_t ReadU16BE(const void * addr)
{
    uint16_t v;
    memcpy(&v, addr, sizeof(uint16_t));
#if (_BYTE_ORDER == _LITTLE_ENDIAN)
    intrinsic_bswap16(v);
#endif
    return v;
}

static uint32_t ReadU32BE(const void * addr)
{
    uint32_t v;
    memcpy(&v, addr, sizeof(uint32_t));
#if _BYTE_ORDER == _LITTLE_ENDIAN
    intrinsic_bswap32(v);
#endif
    return v;
}

static uint64_t ReadU64BE(const void * addr)
{
    uint64_t v;
    memcpy(&v, addr, sizeof(uint64_t));
#if _BYTE_ORDER == _LITTLE_ENDIAN
    intrinsic_bswap64(v);
#endif
    return v;
}

// DTZ tables don't store valid scores for moves that reset the rule50 counter
// like captures and pawn moves but we can easily recover the correct dtz of the
// previous move if we know the position's WDL score.
static int dtz_before_zeroing(WDLScore wdl)
{
    return wdl == WDLWin         ?  1   :
           wdl == WDLCursedWin   ?  101 :
           wdl == WDLBlessedLoss ? -101 :
           wdl == WDLLoss        ? -1   : 0;
}

// Return the sign of a number (-1, 0, 1)
static int SignOf(int val)
{
    return (0 < val) - (val < 0);
}

// Numbers in little endian used by sparseIndex[] to point into blockLength[]
typedef struct SparseEntry
{
    char block[4];   // Number of block
    char offset[2];  // Offset within the block
} SparseEntry;

STATIC_ASSERT(sizeof(SparseEntry) == 6, "SparseEntry must be 6 bytes");

typedef uint16_t Symbol;

typedef struct LR
{
    uint8_t lr[3]; // The first 12 bits is the left-hand symbol, the second 12
                   // bits is the right-hand symbol. If symbol has length 1,
                   // then the left-hand symbol is the stored value.
} LR;

static Symbol LRGetLeft(LR lr)
{
    return ((lr.lr[1] & 0xF) << 8) | lr.lr[0];
}

static Symbol LRGetRight(LR lr)
{
    return (lr.lr[2] << 4) | (lr.lr[1] >> 4);
}

STATIC_ASSERT(sizeof(LR) == 3, "LR tree entry must be 3 bytes");

// Tablebases data layout is structured as following:
//
//  TBFile:   memory maps/unmaps the physical .rtbw and .rtbz files
//  TBTable:  one object for each file with corresponding indexing information
//  TBTables: has ownership of TBTable objects, keeping a list and a hash

static String * TBFilePaths = NULL;
static size_t TBNumFilePaths = 0;

// struct PairsData contains low level indexing information to access TB data.
// There are 8, 4 or 2 PairsData records for each TBTable, according to type of
// table and if positions have pawns or not. It is populated at first access.
typedef struct PairsData
{
    uint8_t flags;                 // Table flags, see enum TBFlag
    uint8_t maxSymLen;             // Maximum length in bits of the Huffman symbols
    uint8_t minSymLen;             // Minimum length in bits of the Huffman symbols
    uint32_t blocksNum;            // Number of blocks in the TB file
    size_t sizeofBlock;            // Block size in bytes
    size_t span;                   // About every span values there is a SparseIndex[] entry
    Symbol* lowestSym;                // lowestSym[l] is the symbol of length l with the lowest value
    LR* btree;                     // btree[sym] stores the left and right symbols that expand sym
    uint16_t* blockLength;         // Number of stored positions (minus one) for each block: 1..65536
    uint32_t blockLengthSize;      // Size of blockLength[] table: padded so it's bigger than blocksNum
    SparseEntry* sparseIndex;      // Partial indices into blockLength[]
    size_t sparseIndexSize;        // Size of SparseIndex[] table
    uint8_t* data;                 // Start of Huffman compressed data
    uint64_t* base64;              // base64[l - min_sym_len] is the 64bit-padded lowest symbol of length l
    int base64Len;
    uint8_t* symlen;               // Number of values (-1) represented by a given Huffman symbol: 1..256
    int symlenLen;
    Piece pieces[TBPIECES];        // Position pieces: the order of pieces defines the groups
    uint64_t groupIdx[TBPIECES+1]; // Start index used for the encoding of the group's pieces
    int groupLen[TBPIECES+1];      // Number of pieces in a given group: KRKN -> (3, 1)
    uint16_t map_idx[4];           // WDLWin, WDLLoss, WDLCursedWin, WDLBlessedLoss (used in DTZ)
} PairsData;

static void PairsDataInit(PairsData * pairsData)
{
    memset(pairsData, 0, sizeof(PairsData));
}

static void PairsDataDestroy(PairsData * pairsData)
{
    if (pairsData->base64Len > 0)
    {
        free(pairsData->base64);
        pairsData->base64Len = 0;
    }

    if (pairsData->symlenLen > 0)
    {
        free(pairsData->symlen);
        pairsData->symlenLen = 0;
    }
}

typedef bool atomic_bool; // TODO

// struct TBTable contains indexing information to access the corresponding TBFile.
// There are 2 types of TBTable, corresponding to a WDL or a DTZ file. TBTable
// is populated at init time but the nested PairsData records are populated at
// first access, when the corresponding file is memory mapped.
//template<TBType Type>
typedef struct TBTable
{
    atomic_bool ready;
    MemoryMappedFile mapping;
    uint8_t* map;
    uint64_t key;
    uint64_t key2;
    int pieceCount;
    bool hasPawns;
    bool hasUniquePieces;
    uint8_t pawnCount[2]; // [Lead color / other color]
    PairsData items[2][4]; // [wtm / btm][FileA..FileD or 0]

    // Linked list
    struct TBTable * next;

} TBTable;

static bool BoardParseCode(Board * board, Player player, const String * code)
{
    assert(StringLength(code) > 0);
    assert(StringCharAt(code, 0) == 'K');

    String weakSide;
    String strongSide;

    if (!StringInitialize(&weakSide))
        return false;

    if (!StringInitialize(&strongSide))
    {
        StringDestroy(&weakSide);
        return false;
    }

    size_t kIndex = StringFindFromIndex(code, 'K', 1);
    if (kIndex == STRING_NOT_FOUND)
        goto err;
    StringSub(code, kIndex, StringLength(code), &weakSide);
    size_t vIndex = StringFind(code, 'v');
    if (vIndex == STRING_NOT_FOUND)
        goto err;
    StringSub(code, 0, min(vIndex, kIndex), &strongSide);

    assert(StringLength(&weakSide) > 0 && StringLength(&weakSide) < 8);
    assert(StringLength(&strongSide) > 0 && StringLength(&strongSide) < 8);

    StringToLowerInPlace(player == White ? &weakSide : &strongSide);

    char fen[128]; // The max is somewhere around 90-92. Give some extra just in case. See https://chess.stackexchange.com/questions/30004/longest-possible-fen
    size_t numWritten = snprintf(fen, sizeof(fen), "8/%s%c/8/8/8/8/%s%c/8 w - - 0 10", StringGetChars(&weakSide), (char) ('8' - StringLength(&weakSide)), StringGetChars(&strongSide), (char) ('8' - StringLength(&strongSide)));

    StringDestroy(&weakSide);
    StringDestroy(&strongSide);

    if (numWritten == 128)
        return false; // Out of memory (probably)

    fen[99] = 0;

    return ParseFEN(fen, board);

err:
    StringDestroy(&weakSide);
    StringDestroy(&strongSide);
    return false;
}

static bool TBTableInit(TBTable * table, const String * code)
{
    Board board;

    if (!BoardParseCode(&board, White, code))
        return false;

    table->ready = false;
    table->key = board.materialHash;
    table->pieceCount = (int)intrinsic_popcnt64(board.allPieceTables);
    table->hasPawns = (board.whitePieceTables[PIECE_TABLE_PAWNS] | board.blackPieceTables[PIECE_TABLE_PAWNS]) > 0;

    table->hasUniquePieces = false;
    for (PieceType i = Pawn; i <= NUM_PIECE_TYPES; ++i)
    {
        if (intrinsic_popcnt64(board.whitePieceTables[i]) == 1 ||
            intrinsic_popcnt64(board.blackPieceTables[i]) == 1)
        {
            table->hasUniquePieces = true;
            break;
        }
    }

    // Set the leading color. In case both sides have pawns the leading color
    // is the side with less pawns because this leads to better compression.
    if ((board.blackPieceTables[PIECE_TABLE_PAWNS] == 0) ||
        (board.whitePieceTables[PIECE_TABLE_PAWNS] > 0 && board.blackPieceTables[PIECE_TABLE_PAWNS] >= board.whitePieceTables[PIECE_TABLE_PAWNS]))
    {
        table->pawnCount[0] = (uint8_t) intrinsic_popcnt64(board.whitePieceTables[PIECE_TABLE_PAWNS]);
        table->pawnCount[1] = (uint8_t) intrinsic_popcnt64(board.blackPieceTables[PIECE_TABLE_PAWNS]);
    }
    else
    {
        table->pawnCount[0] = (uint8_t) intrinsic_popcnt64(board.blackPieceTables[PIECE_TABLE_PAWNS]);
        table->pawnCount[1] = (uint8_t) intrinsic_popcnt64(board.whitePieceTables[PIECE_TABLE_PAWNS]);
    }

    // This is kind of a necessary evil. I suppose it would be possible to write a Zobrist hashing function
    // to switch all of the colors of all the pieces, but that seems like overkill for this, especially
    // since this is run only on initialization anyway.
    if (!BoardParseCode(&board, Black, code))
        return false;

    table->key2 = board.materialHash;

    MemoryMappedFileInitialize(&table->mapping);

    memset(&table->items, 0, sizeof(table->items));
    table->next = NULL;

    return true;
}

static void TBTableInitCopy(TBTable * table, const TBTable * wdl)
{
    table->ready = false;
    MemoryMappedFileInitialize(&table->mapping);

    // Use the corresponding WDL table to avoid recalculating all from scratch
    table->key = wdl->key;
    table->key2 = wdl->key2;
    table->pieceCount = wdl->pieceCount;
    table->hasPawns = wdl->hasPawns;
    table->hasUniquePieces = wdl->hasUniquePieces;
    table->pawnCount[0] = wdl->pawnCount[0];
    table->pawnCount[1] = wdl->pawnCount[1];
    memset(&table->items, 0, sizeof(table->items));
    table->next = NULL;
}

static void TBTableDestroy(TBTable * table)
{
    MemoryMappedFileDestroy(&table->mapping);
}

static PairsData * TBTableGetWDL(TBTable * table, int side, int f)
{
    return &table->items[side % 2][table->hasPawns ? f : 0];
}

static PairsData * TBTableGetDTZ(TBTable * table, int f)
{
    return &table->items[0][table->hasPawns ? f : 0];
}

static PairsData * TBTableGet(TBTable * table, int side, int f, TBType type)
{
    assert(side < 2);
    return &table->items[type == TBTypeWDL ? side : 0][table->hasPawns ? f : 0];
}

// 4K table, indexed by key's 12 lsb
#define TB_TABLES_SIZE (1 << 12)

// Number of elements allowed to map to the last bucket
#define TB_TABLES_OVERFLOW 1

typedef struct TBTablesEntry
{
    uint64_t key;
    TBTable * wdl;
    TBTable * dtz;
} TBTablesEntry;

typedef struct TBTables
{
    TBTablesEntry hashTable[TB_TABLES_SIZE + TB_TABLES_OVERFLOW];
    TBTable * wdlTable;
    TBTable * wdlTableEnd;
    TBTable * dtzTable;
    TBTable * dtzTableEnd;
    uint32_t size;
} TBTables;

static TBTables s_tbTables;

static void TBTablesClear(TBTables * tables);

static void TBTablesInit(TBTables * tables)
{
    memset(tables->hashTable, 0, sizeof(tables->hashTable));
    tables->wdlTable = NULL;
    tables->wdlTableEnd = NULL;
    tables->dtzTable = NULL;
    tables->dtzTableEnd = NULL;
}

static void TBTablesDestroy(TBTables * tables)
{
    TBTablesClear(tables);
}

static bool TBTablesInsert(TBTables * tables, uint64_t key, TBTable * wdl, TBTable * dtz)
{
    uint32_t homeBucket = (uint32_t) key & (TB_TABLES_SIZE - 1);
    TBTablesEntry entry;
    entry.key = key;
    entry.wdl = wdl;
    entry.dtz = dtz;

    // Ensure last element is empty to avoid overflow when looking up
    for (uint32_t bucket = homeBucket; bucket < TB_TABLES_SIZE + TB_TABLES_OVERFLOW - 1; ++bucket)
    {
        uint64_t otherKey = tables->hashTable[bucket].key;
        if (otherKey == key || tables->hashTable[bucket].wdl == NULL)
        {
            tables->hashTable[bucket] = entry;
            return true;
        }

        // Robin Hood hashing: If we've probed for longer than this element,
        // insert here and search for a new spot for the other element instead.
        uint32_t otherHomeBucket = (uint32_t) otherKey & (TB_TABLES_SIZE - 1);
        if (otherHomeBucket > homeBucket)
        {
            TBTablesEntry temp = entry;
            entry = tables->hashTable[bucket];
            tables->hashTable[bucket] = temp;
            key = otherKey;
            homeBucket = otherHomeBucket;
        }
    }
    fprintf(stderr, "TB hash table size too low!\n");
    return false;
}

static bool TBTablesAdd(TBTables * tables, const PieceType * pieces, int numPieces)
{
    String code;

    if (!StringInitialize(&code))
        return false;

    for (int i = 0; i < numPieces; ++i)
    {
        if (!StringPush(&code, PieceToChar[pieces[i]]))
            goto err;
    }

    size_t kIndex = StringFindFromIndex(&code, 'K', 1);
    if (kIndex == STRING_NOT_FOUND)
        goto err;

    if (!StringInsert(&code, kIndex, 'v'))
        goto err;

    MemoryMappedFile m;
    MemoryMappedFileInitialize(&m);

    String path;
    StringInitialize(&path);
    bool mappedFile = false;
    for (size_t i = 0; i < TBNumFilePaths; ++i)
    {
        if (!StringCopy(&path, &TBFilePaths[i]))
            break;
#ifdef _WIN32
        if (!StringPush(&path, '\\'))
            break;
#else
        if (!StringPush(&path, '/'))
            break;
#endif
        if (!StringConcat(&path, &code))
            break;
        if (!StringAppend(&path, ".rtbw"))
            break;
        if (MemoryMappedFileOpen(&m, StringGetChars(&path)))
        {
            mappedFile = true;
            break;
        }
    }

    StringDestroy(&path);

    if (!mappedFile)
    {
        MemoryMappedFileDestroy(&m);
        goto err;
    }

    uint64_t size = MemoryMappedFileGetSize(&m);
    MemoryMappedFileDestroy(&m);

    if (size % 64 != 16)
    {
        fprintf(stderr, "Corrupt tablebase file %s.rtbw\n", StringGetChars(&code));
        goto err;
    }

    TBTable * wdl;
    TBTable * dtz;

    wdl = (TBTable *) malloc(sizeof(TBTable));
    if (wdl == NULL)
        goto err;

    if (!TBTableInit(wdl, &code))
    {
        free(wdl);
        goto err;
    }

    dtz = (TBTable *) malloc(sizeof(TBTable));
    if (dtz == NULL)
    {
        TBTableDestroy(wdl);
        free(wdl);
        goto err;
    }

    TBTableInitCopy(dtz, wdl);

    if (tables->wdlTable == NULL)
    {
        tables->wdlTable = wdl;
        tables->wdlTableEnd = tables->wdlTable;

        tables->dtzTable = dtz;
        tables->dtzTableEnd = tables->dtzTable;
    }
    else
    {
        tables->wdlTableEnd->next = wdl;
        tables->wdlTableEnd = tables->wdlTableEnd->next;

        tables->dtzTableEnd->next = dtz;
        tables->dtzTableEnd = tables->dtzTableEnd->next;
    }

    tables->size++;

    StringDestroy(&code);

    MaxCardinality = max(numPieces, MaxCardinality);

    // Insert into the hash keys for both colors: KRvK with KR white and black
    return TBTablesInsert(tables, tables->wdlTableEnd->key, tables->wdlTableEnd, tables->dtzTableEnd) &&
           TBTablesInsert(tables, tables->wdlTableEnd->key2, tables->wdlTableEnd, tables->dtzTableEnd);

err:
    StringDestroy(&code);
    return false;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6001) // MSVC gives an incorrect unintialized memory warning
#endif

static void TBTablesClear(TBTables * tables)
{
    TBTable * next = NULL;
    while (tables->wdlTable != NULL)
    {
        next = tables->wdlTable->next;
        TBTableDestroy(tables->wdlTable);
        free(tables->wdlTable);
        tables->wdlTable = next;
    }
    while (tables->dtzTable != NULL)
    {
        next = tables->dtzTable->next;
        TBTableDestroy(tables->dtzTable);
        free(tables->dtzTable);
        tables->dtzTable = next;
    }
    memset(tables->hashTable, 0, sizeof(tables->hashTable));
    tables->wdlTableEnd = NULL;
    tables->dtzTableEnd = NULL;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

static uint32_t TBTablesSize(const TBTables * tables)
{
    return tables->size;
}

static TBTable * TBTablesGet(TBTables * tables, uint64_t key, TBType type)
{
    for (size_t i = (size_t) (key & (TB_TABLES_SIZE - 1)); i < TB_TABLES_SIZE + TB_TABLES_OVERFLOW; ++i)
    {
        const TBTablesEntry * entry = &tables->hashTable[i];
        if (entry->key != 0)
            printf("");
        if (entry->key == key)
            return type == TBTypeWDL ? entry->wdl : entry->dtz;
        else if (type == TBTypeWDL && entry->wdl == NULL)
            return NULL;
        else if (type == TBTypeDTZ && entry->dtz == NULL)
            return NULL;
    }
    return NULL;
}

// TB tables are compressed with canonical Huffman code. The compressed data is divided into
// blocks of size d->sizeofBlock, and each block stores a variable number of symbols.
// Each symbol represents either a WDL or a (remapped) DTZ value, or a pair of other symbols
// (recursively). If you keep expanding the symbols in a block, you end up with up to 65536
// WDL or DTZ values. Each symbol represents up to 256 values and will correspond after
// Huffman coding to at least 1 bit. So a block of 32 bytes corresponds to at most
// 32 x 8 x 256 = 65536 values. This maximum is only reached for tables that consist mostly
// of draws or mostly of wins, but such tables are actually quite common. In principle, the
// blocks in WDL tables are 64 bytes long (and will be aligned on cache lines). But for
// mostly-draw or mostly-win tables this can leave many 64-byte blocks only half-filled, so
// in such cases blocks are 32 bytes long. The blocks of DTZ tables are up to 1024 bytes long.
// The generator picks the size that leads to the smallest table. The "book" of symbols and
// Huffman codes is the same for all blocks in the table. A non-symmetric pawnless TB file
// will have one table for wtm and one for btm, a TB file with pawns will have tables per
// file a,b,c,d also in this case one set for wtm and one for btm.
static int decompress_pairs(PairsData * d, uint64_t idx)
{
    // Special case where all table positions store the same value
    if (d->flags & TBFlagSingleValue)
        return d->minSymLen;

    // First we need to locate the right block that stores the value at index "idx".
    // Because each block n stores blockLength[n] + 1 values, the index i of the block
    // that contains the value at position idx is:
    //
    //                    for (i = -1, sum = 0; sum <= idx; i++)
    //                        sum += blockLength[i + 1] + 1;
    //
    // This can be slow, so we use SparseIndex[] populated with a set of SparseEntry that
    // point to known indices into blockLength[]. Namely SparseIndex[k] is a SparseEntry
    // that stores the blockLength[] index and the offset within that block of the value
    // with index I(k), where:
    //
    //       I(k) = k * d->span + d->span / 2      (1)

    // First step is to get the 'k' of the I(k) nearest to our idx, using definition (1)
    uint32_t k = (uint32_t)(idx / d->span);

    // Then we read the corresponding SparseIndex[] entry
    uint32_t block = ReadU32LE(&d->sparseIndex[k].block);
    int offset     = ReadU16LE(&d->sparseIndex[k].offset);

    // Now compute the difference idx - I(k). From definition of k we know that
    //
    //       idx = k * d->span + idx % d->span    (2)
    //
    // So from (1) and (2) we can compute idx - I(K):
    int diff = (int)(idx % d->span - d->span / 2);

    // Sum the above to offset to find the offset corresponding to our idx
    offset += diff;

    // Move to previous/next block, until we reach the correct block that contains idx,
    // that is when 0 <= offset <= d->blockLength[block]
    while (offset < 0)
        offset += d->blockLength[--block] + 1;

    while (offset > d->blockLength[block])
        offset -= d->blockLength[block++] + 1;

    // Finally, we find the start address of our block of canonical Huffman symbols
    uint32_t* ptr = (uint32_t*)(d->data + ((uint64_t)block * d->sizeofBlock));

    // Read the first 64 bits in our block, this is a (truncated) sequence of
    // unknown number of symbols of unknown length but we know the first one
    // is at the beginning of this 64 bits sequence.
    uint64_t buf64 = ReadU64BE(ptr);
    ptr += 2;
    int buf64Size = 64;
    Symbol sym;

    while (true)
    {
        int len = 0; // This is the symbol length - d->min_sym_len

        // Now get the symbol length. For any symbol s64 of length l right-padded
        // to 64 bits we know that d->base64[l-1] >= s64 >= d->base64[l] so we
        // can find the symbol length iterating through base64[].
        while (buf64 < d->base64[len])
            ++len;

        // All the symbols of a given length are consecutive integers (numerical
        // sequence property), so we can compute the offset of our symbol of
        // length len, stored at the beginning of buf64.
        sym = (Symbol)((buf64 - d->base64[len]) >> (64 - len - d->minSymLen));

        // Now add the value of the lowest symbol of length len to get our symbol
        sym += ReadU16LE(&d->lowestSym[len]);

        // If our offset is within the number of values represented by symbol sym
        // we are done...
        if (offset < d->symlen[sym] + 1)
            break;

        // ...otherwise update the offset and continue to iterate
        offset -= d->symlen[sym] + 1;
        len += d->minSymLen; // Get the real length
        buf64 <<= len;       // Consume the just processed symbol
        buf64Size -= len;

        if (buf64Size <= 32) // Refill the buffer
        {
            buf64Size += 32;
            buf64 |= (uint64_t) ReadU32BE(ptr++) << (64 - buf64Size);
        }
    }

    // Ok, now we have our symbol that expands into d->symlen[sym] + 1 symbols.
    // We binary-search for our value recursively expanding into the left and
    // right child symbols until we reach a leaf node where symlen[sym] + 1 == 1
    // that will store the value we need.
    while (d->symlen[sym])
    {
        Symbol left = LRGetLeft(d->btree[sym]);

        // If a symbol contains 36 sub-symbols (d->symlen[sym] + 1 = 36) and
        // expands in a pair (d->symlen[left] = 23, d->symlen[right] = 11), then
        // we know that, for instance the ten-th value (offset = 10) will be on
        // the left side because in Recursive Pairing child symbols are adjacent.
        if (offset < d->symlen[left] + 1)
        {
            sym = left;
        }
        else
        {
            offset -= d->symlen[left] + 1;
            sym = LRGetRight(d->btree[sym]);
        }
    }

    return LRGetLeft(d->btree[sym]);
}

static bool CheckDTZSTM(TBTable * entry, int stm, File f, TBType type)
{
    if (type == TBTypeWDL)
        return true;
    else
    {
        uint8_t flags = TBTableGetDTZ(entry, f)->flags;
        return (flags & TBFlagSTM) == stm ||
               ((entry->key == entry->key2) && !entry->hasPawns);
    }
}

// DTZ scores are sorted by frequency of occurrence and then assigned the
// values 0, 1, 2, ... in order of decreasing frequency. This is done for each
// of the four WDLScore values. The mapping information necessary to reconstruct
// the original values is stored in the TB file and read during map[] init.
static int MapScore(TBTable * table, File f, int value, WDLScore wdl, TBType type)
{
    if (type == TBTypeWDL)
    {
        return value - 2;
    }
    else
    {
        static const int WDLMap[] = { 1, 3, 0, 2, 0 };

        PairsData * data = TBTableGetDTZ(table, f);
        uint8_t flags = data->flags;
        const uint16_t * idx = data->map_idx;
        const uint8_t * map = table->map;

        if (flags & TBFlagMapped)
        {
            if (flags & TBFlagWide)
                value = ((const uint16_t *) map)[idx[WDLMap[wdl + 2]] + value];
            else
                value = map[idx[WDLMap[wdl + 2]] + value];
        }

        // DTZ tables store distance to zero in number of moves or plies. We
        // want to return plies, so we have convert to plies when needed.
        if ((wdl == WDLWin && !(flags & TBFlagWinPlies)) ||
            (wdl == WDLLoss && !(flags & TBFlagLossPlies)) ||
            wdl == WDLCursedWin ||
            wdl == WDLBlessedLoss)
        {
            value *= 2;
        }

        return value + 1;
    }
}

// Compute a unique index out of a position and use it to probe the TB file. To
// encode k pieces of same type and color, first sort the pieces by square in
// ascending order s1 <= s2 <= ... <= sk then compute the unique index as:
//
//      idx = Binomial[1][s1] + Binomial[2][s2] + ... + Binomial[k][sk]
//
static int DoProbeTable(const Board * board, TBTable * table, TBType type, WDLScore wdl, ProbeState * result)
{
    Square squares[TBPIECES] = { 0 };
    Piece pieces[TBPIECES] = { 0 };
    uint64_t idx;
    int next = 0, size = 0, leadPawnsCnt = 0;
    PairsData * d;
    uint64_t b, leadPawns = 0;
    File tbFile = FileA;

    // A given TB entry like KRK has associated two material keys: KRvk and Kvkr.
    // If both sides have the same pieces keys are equal. In this case TB tables
    // only store the 'white to move' case, so if the position to lookup has black
    // to move, we need to switch the color and flip the squares before to lookup.
    bool symmetricBlackToMove = (table->key == table->key2 && board->playerToMove == Black);

    // TB files are calculated for white as stronger side. For instance we have
    // KRvK, not KvKR. A position where stronger side is white will have its
    // material key == table->key, otherwise we have to switch the color and
    // flip the squares before to lookup.
    bool blackStronger = (board->materialHash != table->key);

    bool flipColor = (symmetricBlackToMove || blackStronger);
    int verticalFlip = flipColor ? 56 : 0;
    int stm = (symmetricBlackToMove || blackStronger) ^ (board->playerToMove == Black);

    // For pawns, TB files store 4 separate tables according if leading pawn is on
    // file a, b, c or d after reordering. The leading pawn is the one with maximum
    // MapPawns[] value, that is the one most toward the edges and with lowest rank.
    if (table->hasPawns)
    {
        // In all the 4 tables, pawns are at the beginning of the piece sequence and
        // their color is the reference one. So we just pick the first one.
        Piece pc = TBTableGet(table, 0, 0, type)->pieces[0];
        if (flipColor)
            pc.player = !pc.player;

        assert(pc.type == Pawn);
        b = BoardGetPieceTable(board, pc.player, pc.type);
        leadPawns = b;

        while (b != 0)
        {
            squares[size++] = SquareDecodeLowest(b) ^ verticalFlip;
            b = intrinsic_blsr64(b);
        }

        leadPawnsCnt = size;

        Square tmp = squares[0];
        Square max = 0;
        for (size_t i = 1; i < leadPawnsCnt; ++i)
        {
            if (MapPawns[squares[i]] > MapPawns[squares[max]])
                max = (Square)i;
        }
        squares[0] = squares[max];
        squares[max] = tmp;

        File file = SquareGetFile(squares[0]);
        tbFile = min(file, FileH - file);
    }

    // DTZ tables are one-sided, i.e. they store positions only for white to
    // move or only for black to move, so check for side to move to be stm,
    // early exit otherwise.
    if (!CheckDTZSTM(table, stm, tbFile, type))
    {
        *result = PROBE_STATE_CHANGE_STM;
        return WDLDraw;
    }

    // Now we are ready to get all the position pieces (but the lead pawns) and
    // directly map them to the correct color and square.
    b = board->allPieceTables ^ leadPawns;
    while (b != 0)
    {
        Square s = SquareDecodeLowest(b);
        squares[size] = s ^ verticalFlip;
        Piece piece;
        BoardGetPlayerPieceAtSquare(board, s, &piece.type, &piece.player);
        if (flipColor)
            piece.player = !piece.player;
        pieces[size++] = piece;
        b = intrinsic_blsr64(b);
    }

    assert(size >= 2);

    d = TBTableGet(table, stm, tbFile, type);

    // Then we reorder the pieces to have the same sequence as the one stored
    // in pieces[i]: the sequence that ensures the best compression.
    for (int i = leadPawnsCnt; i < size - 1; ++i)
    {
        for (int j = i + 1; j < size; ++j)
        {
            if (PIECE_EQUALS(d->pieces[i], pieces[j]))
            {
                Piece tmpPc = pieces[j];
                pieces[j] = pieces[i];
                pieces[i] = tmpPc;

                Square tmpSq = squares[j];
                squares[j] = squares[i];
                squares[i] = tmpSq;
                break;
            }
        }
    }

    // Now we map again the squares so that the square of the lead piece is in
    // the triangle A1-D1-D4.
    if (SquareGetFile(squares[0]) > FileD)
    {
        for (int i = 0; i < size; ++i)
            squares[i] = SQUARE_HORIZONTAL_FLIP(squares[i]);
    }

    // Encode leading pawns starting with the one with minimum MapPawns[] and
    // proceeding in ascending order.
    if (table->hasPawns)
    {
        idx = LeadPawnIdx[leadPawnsCnt][squares[0]];

        StableSort(squares + 1, squares + leadPawnsCnt, sizeof(Square), pawns_comp);

        for (int i = 1; i < leadPawnsCnt; ++i)
            idx += Binomial[i][MapPawns[squares[i]]];

        goto encode_remaining; // With pawns we have finished special treatments
    }

    // In positions without pawns, we further flip the squares to ensure leading
    // piece is below RANK_5.
    if (SquareGetRank(squares[0]) > Rank4)
    {
        for (int i = 0; i < size; ++i)
            squares[i] = SQUARE_VERTICAL_FLIP(squares[i]);
    }

    // Look for the first piece of the leading group not on the A1-D4 diagonal
    // and ensure it is mapped below the diagonal.
    for (int i = 0; i < d->groupLen[0]; ++i)
    {
        if (!off_A1H8(squares[i]))
            continue;

        if (off_A1H8(squares[i]) > 0) // A1-H8 diagonal flip: SQ_A3 -> SQ_C1
        {
            for (int j = i; j < size; ++j)
                squares[j] = ((squares[j] >> 3) | (squares[j] << 3)) & 63;
        }
        break;
    }

    // Encode the leading group.
    //
    // Suppose we have KRvK. Let's say the pieces are on square numbers wK, wR
    // and bK (each 0...63). The simplest way to map this position to an index
    // is like this:
    //
    //   index = wK * 64 * 64 + wR * 64 + bK;
    //
    // But this way the TB is going to have 64*64*64 = 262144 positions, with
    // lots of positions being equivalent (because they are mirrors of each
    // other) and lots of positions being invalid (two pieces on one square,
    // adjacent kings, etc.).
    // Usually the first step is to take the wK and bK together. There are just
    // 462 ways legal and not-mirrored ways to place the wK and bK on the board.
    // Once we have placed the wK and bK, there are 62 squares left for the wR
    // Mapping its square from 0..63 to available squares 0..61 can be done like:
    //
    //   wR -= (wR > wK) + (wR > bK);
    //
    // In words: if wR "comes later" than wK, we deduct 1, and the same if wR
    // "comes later" than bK. In case of two same pieces like KRRvK we want to
    // place the two Rs "together". If we have 62 squares left, we can place two
    // Rs "together" in 62 * 61 / 2 ways (we divide by 2 because rooks can be
    // swapped and still get the same position.)
    //
    // In case we have at least 3 unique pieces (included kings) we encode them
    // together.
    if (table->hasUniquePieces)
    {
        int adjust1 = squares[1] > squares[0];
        int adjust2 = (squares[2] > squares[0]) + (squares[2] > squares[1]);

        // First piece is below a1-h8 diagonal. MapA1D1D4[] maps the b1-d1-d3
        // triangle to 0...5. There are 63 squares for second piece and and 62
        // (mapped to 0...61) for the third.
        if (off_A1H8(squares[0]))
        {
            idx = (MapA1D1D4[squares[0]] * 63
                   + (squares[1] - adjust1)) * 62
                + squares[2] - adjust2;
        }
        // First piece is on a1-h8 diagonal, second below: map this occurrence to
        // 6 to differentiate from the above case, SquareGetRank() maps a1-d4 diagonal
        // to 0...3 and finally MapB1H1H7[] maps the b1-h1-h7 triangle to 0..27.
        else if (off_A1H8(squares[1]))
        {
            idx = (6 * 63 + SquareGetRank(squares[0]) * 28
                   + MapB1H1H7[squares[1]]) * 62
                + squares[2] - adjust2;
        }
        // First two pieces are on a1-h8 diagonal, third below
        else if (off_A1H8(squares[2]))
        {
            idx = 6 * 63 * 62 + 4 * 28 * 62
                + SquareGetRank(squares[0]) * 7 * 28
                + (SquareGetRank(squares[1]) - adjust1) * 28
                + MapB1H1H7[squares[2]];
        }
        // All 3 pieces on the diagonal a1-h8
        else
        {
            idx = 6 * 63 * 62 + 4 * 28 * 62 + 4 * 7 * 28
                + SquareGetRank(squares[0]) * 7 * 6
                + (SquareGetRank(squares[1]) - adjust1) * 6
                + (SquareGetRank(squares[2]) - adjust2);
        }
    }
    else
    {
        // We don't have at least 3 unique pieces, like in KRRvKBB, just map
        // the kings.
        idx = MapKK[MapA1D1D4[squares[0]]][squares[1]];
    }

encode_remaining:
    idx *= d->groupIdx[0];
    Square * groupSq = squares + d->groupLen[0];

    // Encode remaining pawns then pieces according to square, in ascending order
    bool remainingPawns = table->hasPawns && table->pawnCount[1];

    while (d->groupLen[++next])
    {
        StableSort(groupSq, groupSq + d->groupLen[next], sizeof(Square), square_comp);
        uint64_t n = 0;

        // Map down a square if "comes later" than a square in the previous
        // groups (similar to what done earlier for leading group pieces).
        for (int i = 0; i < d->groupLen[next]; ++i)
        {
            size_t adjust = 0;
            for (Square * iter = squares; iter != groupSq; ++iter)
            {
                if (groupSq[i] > *iter)
                    adjust++;
            }
            n += Binomial[i + 1][groupSq[i] - adjust - 8 * remainingPawns];
        }

        remainingPawns = false;
        idx += n * d->groupIdx[next];
        groupSq += d->groupLen[next];
    }

    // Now that we have the index, decompress the pair and get the score
    return MapScore(table, tbFile, decompress_pairs(d, idx), wdl, type);
}

// Group together pieces that will be encoded together. The general rule is that
// a group contains pieces of same type and color. The exception is the leading
// group that, in case of positions without pawns, can be formed by 3 different
// pieces (default) or by the king pair when there is not a unique piece apart
// from the kings. When there are pawns, pawns are always first in pieces[].
//
// As example KRKN -> KRK + N, KNNK -> KK + NN, KPPKP -> P + PP + K + K
//
// The actual grouping depends on the TB generator and can be inferred from the
// sequence of pieces in piece[] array.
static void SetGroups(TBTable * table, PairsData * d, int order[], File f)
{
    int n = 0, firstLen = table->hasPawns ? 0 : table->hasUniquePieces ? 3 : 2;
    d->groupLen[n] = 1;

    // Number of pieces per group is stored in groupLen[], for instance in KRKN
    // the encoder will default on '111', so groupLen[] will be (3, 1).
    for (int i = 1; i < table->pieceCount; ++i)
        if (--firstLen > 0 || PIECE_EQUALS(d->pieces[i], d->pieces[i - 1]))
            d->groupLen[n]++;
        else
            d->groupLen[++n] = 1;

    d->groupLen[++n] = 0; // Zero-terminated

    // The sequence in pieces[] defines the groups, but not the order in which
    // they are encoded. If the pieces in a group g can be combined on the board
    // in N(g) different ways, then the position encoding will be of the form:
    //
    //           g1 * N(g2) * N(g3) + g2 * N(g3) + g3
    //
    // This ensures unique encoding for the whole position. The order of the
    // groups is a per-table parameter and could not follow the canonical leading
    // pawns/pieces -> remaining pawns -> remaining pieces. In particular the
    // first group is at order[0] position and the remaining pawns, when present,
    // are at order[1] position.
    bool pp = table->hasPawns && table->pawnCount[1]; // Pawns on both sides
    int next = pp ? 2 : 1;
    int freeSquares = 64 - d->groupLen[0] - (pp ? d->groupLen[1] : 0);
    uint64_t idx = 1;

    for (int k = 0; next < n || k == order[0] || k == order[1]; ++k)
        if (k == order[0]) // Leading pawns or pieces
        {
            d->groupIdx[0] = idx;
            idx *= table->hasPawns ? LeadPawnsSize[d->groupLen[0]][f]
                : table->hasUniquePieces ? 31332 : 462;
        }
        else if (k == order[1]) // Remaining pawns
        {
            d->groupIdx[1] = idx;
            idx *= Binomial[d->groupLen[1]][48 - d->groupLen[0]];
        }
        else // Remaining pieces
        {
            d->groupIdx[next] = idx;
            idx *= Binomial[d->groupLen[next]][freeSquares];
            freeSquares -= d->groupLen[next++];
        }

    d->groupIdx[n] = idx;
}

// In Recursive Pairing each symbol represents a pair of children symbols. So
// read d->btree[] symbols data and expand each one in his left and right child
// symbol until reaching the leafs that represent the symbol value.
static uint8_t SetSymLen(PairsData * d, Symbol s, bool * visited)
{
    visited[s] = true; // We can set it now because tree is acyclic
    Symbol sr = LRGetRight(d->btree[s]);

    if (sr == 0xFFF)
        return 0;

    Symbol sl = LRGetLeft(d->btree[s]);

    if (!visited[sl])
        d->symlen[sl] = SetSymLen(d, sl, visited);

    if (!visited[sr])
        d->symlen[sr] = SetSymLen(d, sr, visited);

    return d->symlen[sl] + d->symlen[sr] + 1;
}

static uint8_t * SetSizes(PairsData * d, uint8_t * data)
{
    d->flags = *data++;

    if (d->flags & TBFlagSingleValue)
    {
        d->blocksNum = d->blockLengthSize = 0;
        d->span = d->sparseIndexSize = 0; // Broken MSVC zero-init
        d->minSymLen = *data++; // Here we store the single value
        return data;
    }

    // groupLen[] is a zero-terminated list of group lengths, the last groupIdx[]
    // element stores the biggest index that is the tb size.
    int gidx = 0;
    for (; gidx < sizeof(d->groupLen) / sizeof(d->groupLen[0]) - 1; gidx++)
    {
        if (d->groupLen[gidx] == 0)
            break;
    }
    uint64_t tbSize = d->groupIdx[gidx];

    d->sizeofBlock = 1ULL << *data++;
    d->span = 1ULL << *data++;
    d->sparseIndexSize = (size_t)((tbSize + d->span - 1) / d->span); // Round up
    uint8_t padding = ReadU8(data++);
    d->blocksNum = ReadU32LE(data);
    data += sizeof(uint32_t);
    d->blockLengthSize = d->blocksNum + padding; // Padded to ensure SparseIndex[]
    // does not point out of range.
    d->maxSymLen = *data++;
    d->minSymLen = *data++;
    d->lowestSym = (Symbol *) data;
    d->base64Len = d->maxSymLen - d->minSymLen + 1;
    if (d->base64 != NULL)
        free(d->base64);
    d->base64 = (uint64_t *)malloc(d->base64Len * sizeof(uint64_t)); // Don't need calloc here; entire memory is immediately written anyway.
    if (d->base64 == NULL)
        exit(EXIT_FAILURE);

    d->base64[d->base64Len - 1] = 0;

    // The canonical code is ordered such that longer symbols (in terms of
    // the number of bits of their Huffman code) have lower numeric value,
    // so that d->lowestSym[i] >= d->lowestSym[i+1] (when read as LittleEndian).
    // Starting from this we compute a base64[] table indexed by symbol length
    // and containing 64 bit values so that d->base64[i] >= d->base64[i+1].
    // See https://en.wikipedia.org/wiki/Huffman_coding
    for (int i = d->base64Len - 2; i >= 0; --i)
    {
        d->base64[i] = (d->base64[i + 1] + ReadU16LE(&d->lowestSym[i]) - ReadU16LE(&d->lowestSym[i + 1])) / 2;

        assert(d->base64[i] * 2 >= d->base64[i + 1]);
    }

    // Now left-shift by an amount so that d->base64[i] gets shifted 1 bit more
    // than d->base64[i+1] and given the above assert condition, we ensure that
    // d->base64[i] >= d->base64[i+1]. Moreover for any symbol s64 of length i
    // and right-padded to 64 bits holds d->base64[i-1] >= s64 >= d->base64[i].
    for (size_t i = 0; i < d->base64Len; ++i)
        d->base64[i] <<= 64 - i - d->minSymLen; // Right-padding to 64 bits

    data += d->base64Len * sizeof(Symbol);
    d->symlenLen = ReadU16LE(data);
    if (d->symlen != NULL)
        free(d->symlen);
    d->symlen = (uint8_t *) calloc(d->symlenLen, sizeof(uint8_t)); // calloc needed here to initialize all symbols to zero
    if (d->symlen == NULL)
        exit(EXIT_FAILURE);
    data += sizeof(uint16_t);
    d->btree = (LR *) data;

    // The compression scheme used is "Recursive Pairing", that replaces the most
    // frequent adjacent pair of symbols in the source message by a new symbol,
    // reevaluating the frequencies of all of the symbol pairs with respect to
    // the extended alphabet, and then repeating the process.
    // See http://www.larsson.dogma.net/dcc99.pdf
    bool * visited = (bool *) calloc(d->symlenLen, sizeof(bool)); // calloc needed here to initialize all items to false
    if (visited == NULL)
        exit(EXIT_FAILURE);

    for (Symbol sym = 0; sym < d->symlenLen; ++sym)
    {
        if (!visited[sym])
            d->symlen[sym] = SetSymLen(d, sym, visited);
    }

    free(visited);

    return data + d->symlenLen * sizeof(LR) + (d->symlenLen & 1);
}

static uint8_t * SetDTZMap(TBTable * table, uint8_t * data, File maxFile, TBType type)
{
    if (type == TBTypeWDL)
    {
        return data;
    }
    else
    {
        table->map = data;

        for (File f = FileA; f <= maxFile; ++f)
        {
            uint8_t flags = TBTableGetDTZ(table, f)->flags;
            if (flags & TBFlagMapped)
            {
                if (flags & TBFlagWide)
                {
                    data += (uintptr_t) data & 1;  // Word alignment, we may have a mixed table
                    for (int i = 0; i < 4; ++i) // Sequence like 3,x,x,x,1,x,0,2,x,x
                    {
                        TBTableGetDTZ(table, f)->map_idx[i] = (uint16_t) ((uint16_t *) data - (uint16_t *) table->map + 1);
                        data += 2 * ReadU16LE(data) + 2;
                    }
                }
                else
                {
                    for (int i = 0; i < 4; ++i)
                    {
                        TBTableGetDTZ(table, f)->map_idx[i] = (uint16_t) (data - table->map + 1);
                        data += *data + 1;
                    }
                }
            }
        }

        return data += (uintptr_t) data & 1; // Word alignment
    }
}

static void Set(TBTable * table, uint8_t * data, TBType type)
{
    PairsData * d;

    enum { Split = 1, HasPawns = 2 };

    assert(table->hasPawns == (bool)(*data & HasPawns));
    assert((table->key != table->key2) == (bool)(*data & Split));

    data++; // First byte stores flags

    const int sides = ((type == TBTypeWDL) && (table->key != table->key2)) ? 2 : 1;
    const File maxFile = table->hasPawns ? FileD : FileA;

    bool pp = table->hasPawns && table->pawnCount[1]; // Pawns on both sides

    assert(!pp || table->pawnCount[0]);

    for (File f = FileA; f <= maxFile; ++f)
    {
        for (int i = 0; i < sides; i++)
        {
            PairsData * pd = TBTableGet(table, i, f, type);
            PairsDataDestroy(pd);
            PairsDataInit(pd);
        }

        int order[][2] = { { *data & 0xF, pp ? *(data + 1) & 0xF : 0xF },
                           { *data >> 4, pp ? *(data + 1) >> 4 : 0xF } };
        data += 1 + pp;

        for (int k = 0; k < table->pieceCount; ++k, ++data)
        {
            for (int i = 0; i < sides; i++)
            {
                TBPiece tbp = (TBPiece)(i ? *data >> 4 : *data & 0xF);
                Piece piece;
                piece.player = (tbp >> 3) & 0x1;
                piece.type = tbp & 0x7;
                TBTableGet(table, i, f, type)->pieces[k] = piece;
            }
        }

        for (int i = 0; i < sides; ++i)
            SetGroups(table, TBTableGet(table, i, f, type), order[i], f);
    }

    data += (uintptr_t) data & 1; // Word alignment

    for (File f = FileA; f <= maxFile; ++f)
    {
        for (int i = 0; i < sides; i++)
            data = SetSizes(TBTableGet(table, i, f, type), data);
    }

    data = SetDTZMap(table, data, maxFile, type);

    for (File f = FileA; f <= maxFile; ++f)
    {
        for (int i = 0; i < sides; i++)
        {
            d = TBTableGet(table, i, f, type);
            d->sparseIndex = (SparseEntry *) data;
            data += d->sparseIndexSize * sizeof(SparseEntry);
        }
    }

    for (File f = FileA; f <= maxFile; ++f)
    {
        for (int i = 0; i < sides; i++)
        {
            d = TBTableGet(table, i, f, type);
            d->blockLength = (uint16_t *) data;
            data += d->blockLengthSize * sizeof(uint16_t);
        }
    }

    for (File f = FileA; f <= maxFile; ++f)
    {
        for (int i = 0; i < sides; i++)
        {
            data = (uint8_t *) (((uintptr_t) data + 0x3F) & ~0x3F); // 64 byte alignment
            d = TBTableGet(table, i, f, type);
            d->data = data;
            data += d->blocksNum * d->sizeofBlock;
        }
    }
}

static Mutex s_mutex;
static atomic_bool s_mutexInitialized;

static bool Mapped(TBTable * table, const Board * board, TBType type)
{
    // Use 'acquire' to avoid a thread reading 'ready' == true while
    // another is still working. (compiler reordering may cause this).
    if (table->ready)
        return MemoryMappedFileGetAddress(&table->mapping) != NULL; // Could be nullptr if file does not exist

    MutexLock(&s_mutex);

    if (table->ready) // Recheck under lock
    {
        bool valid = MemoryMappedFileGetAddress(&table->mapping) != NULL;
        MutexUnlock(&s_mutex);
        return valid;
    }

    bool ret = false;

    // Pieces strings in decreasing order for each color, like ("KPP","KR")
    String fname;
    String w;
    String b;

    if (!StringInitialize(&fname))
        goto err_fname;

    if (!StringInitialize(&w))
        goto err_w;

    if (!StringInitialize(&b))
        goto err_b;

    // Minor optimization: start with Kings, which should always be present.
    if (!StringPush(&w, 'K'))
        goto err;
    if (!StringPush(&b, 'K'))
        goto err;

    unsigned long long count;
    unsigned long long i;
    for (PieceType pt = Queen; pt >= Pawn; --pt)
    {
        count = intrinsic_popcnt64(BoardGetPieceTable(board, White, pt));
        for (i = 0; i < count; ++i)
        {
            if (!StringPush(&w, PieceToChar[pt]))
                goto err;
        }
        count = intrinsic_popcnt64(BoardGetPieceTable(board, Black, pt));
        for (i = 0; i < count; ++i)
        {
            if (!StringPush(&b, PieceToChar[pt]))
                goto err;
        }
    }

    if (table->key == board->materialHash)
    {
        if (!StringConcat(&fname, &w))
            goto err;
        if (!StringPush(&fname, 'v'))
            goto err;
        if (!StringConcat(&fname, &b))
            goto err;
    }
    else
    {
        if (!StringConcat(&fname, &b))
            goto err;
        if (!StringPush(&fname, 'v'))
            goto err;
        if (!StringConcat(&fname, &w))
            goto err;
    }

    if (!StringAppend(&fname, type == TBTypeWDL ? ".rtbw" : ".rtbz"))
        goto err;

    String path;
    StringInitialize(&path);
    bool mappedFile = false;
    for (size_t i = 0; i < TBNumFilePaths; ++i)
    {
        if (!StringCopy(&path, &TBFilePaths[i]))
            break;
#ifdef _WIN32
        if (!StringPush(&path, '\\'))
            break;
#else
        if (!StringPush(&path, '/'))
            break;
#endif
        if (!StringConcat(&path, &fname))
            break;
        if (MemoryMappedFileOpen(&table->mapping, StringGetChars(&path)))
        {
            mappedFile = true;
            break;
        }
    }
    StringDestroy(&path);
    if (!mappedFile)
        goto err;

    uint8_t * address = MemoryMappedFileGetAddress(&table->mapping);

    static const uint8_t Magics[][4] = { { 0x71, 0xE8, 0x23, 0x5D }, // WDL
                                         { 0xD7, 0x66, 0x0C, 0xA5 } }; // DTZ

    if (memcmp(address, Magics[type], 4) != 0)
    {
        fprintf(stderr, "Corrupted table in file %s\n", StringGetChars(&fname));
        MemoryMappedFileClose(&table->mapping);
        goto err;
    }

    uint8_t * data = address + 4; // Skip magic header

    if (data)
        Set(table, data, type);

    table->ready = true;

    ret = MemoryMappedFileGetAddress(&table->mapping) != NULL;

err:
    StringDestroy(&b);
err_b:
    StringDestroy(&w);
err_w:
    StringDestroy(&fname);
err_fname:
    MutexUnlock(&s_mutex);

    return ret;
}

static int ProbeTable(const Board * board, ProbeState * result, WDLScore wdl, TBType type)
{
    if (intrinsic_popcnt64(board->allPieceTables) == 2)
        return 0; // King versus King

    TBTable * entry = TBTablesGet(&s_tbTables, board->materialHash, type);

    if (entry == NULL || !Mapped(entry, board, type))
    {
        *result = PROBE_STATE_FAIL;
        return 0;
    }

    return DoProbeTable(board, entry, type, wdl, result);
}

// For a position where the side to move has a winning capture it is not necessary
// to store a winning value so the generator treats such positions as "don't cares"
// and tries to assign to it a value that improves the compression ratio. Similarly,
// if the side to move has a drawing capture, then the position is at least drawn.
// If the position is won, then the TB needs to store a win value. But if the
// position is drawn, the TB may store a loss value if that is better for compression.
// All of this means that during probing, the engine must look at captures and probe
// their results and must probe the position itself. The "best" result of these
// probes is the correct result for the position.
// DTZ tables do not store values when a following move is a zeroing winning move
// (winning capture or winning pawn move). Also DTZ store wrong values for positions
// where the best move is an ep-move (even if losing). So in all these cases set
// the state to PROBE_STATE_ZEROING_BEST_MOVE.
static WDLScore search(const Board * board, ProbeState * result, bool checkZeroingMoves)
{
    WDLScore value, bestValue = WDLLoss;
    uint64_t numPiecesRemaining = intrinsic_popcnt64(board->allPieceTables);

    // TODO: Pass in moves from evaluation function so we don't need to eat up extra stack space here for no reason.
    Move moves[256];
    uint8_t numMoves = GetValidMoves(board, moves);
    uint8_t movesCounted = 0;
    uint8_t i;
    Board nextBoard;

    //auto moveList = MoveList<LEGAL>(pos);
    //size_t totalCount = moveList.size(), moveCount = 0;

    for (i = 0; i < numMoves; ++i)
    {
        Move move = moves[i];
        nextBoard = *board;
        MakeMove(&nextBoard, move);

        bool isCapture = numPiecesRemaining > intrinsic_popcnt64(nextBoard.allPieceTables);
        if (!isCapture && (!checkZeroingMoves || move.piece == Pawn))
            continue;

        movesCounted++;

        value = -search(&nextBoard, result, false);

        if (*result == PROBE_STATE_FAIL)
            return WDLDraw;

        if (value > bestValue)
        {
            bestValue = value;

            if (value >= WDLWin)
            {
                *result = PROBE_STATE_ZEROING_BEST_MOVE; // Winning DTZ-zeroing move
                return value;
            }
        }
    }

    // In case we have already searched all the legal moves we don't have to probe
    // the TB because the stored score could be wrong. For instance TB tables
    // do not contain information on position with ep rights, so in this case
    // the result of probe_wdl_table is wrong. Also in case of only capture
    // moves, for instance here 4K3/4q3/6p1/2k5/6p1/8/8/8 w - - 0 7, we have to
    // return with PROBE_STATE_ZEROING_BEST_MOVE set.
    bool noMoreMoves = (movesCounted > 0 && movesCounted == numMoves);

    if (noMoreMoves)
        value = bestValue;
    else
    {
        value = (WDLScore)ProbeTable(board, result, WDLDraw, TBTypeWDL);

        if (*result == PROBE_STATE_FAIL)
            return WDLDraw;
    }

    // DTZ stores a "don't care" value if bestValue is a win
    if (bestValue >= value)
    {
        return *result = ((bestValue > WDLDraw || noMoreMoves) ? PROBE_STATE_ZEROING_BEST_MOVE : PROBE_STATE_OK), bestValue;
    }

    return *result = PROBE_STATE_OK, value;
}

bool SyzygyInit(const char * paths)
{
    if (!s_mutexInitialized)
    {
        if (!MutexInitialize(&s_mutex))
            return false;
        s_mutexInitialized = true;
    }

    TBTablesInit(&s_tbTables);
    MaxCardinality = 0;

    if (paths == NULL || paths[0] == '\0' || paths == "<empty>")
        return true;

#ifdef _WIN32
#define SEPARATOR_CHAR ';'
#else
#define SEPARATOR_CHAR ':'
#endif
    size_t numPaths = 1;
    const char * pathsIter = paths;
    while (*pathsIter != '\0')
    {
        if (*pathsIter == SEPARATOR_CHAR)
            numPaths++;
        pathsIter++;
    }
    TBFilePaths = (String *) malloc(numPaths * sizeof(String));
    if (TBFilePaths == NULL)
        goto err;
    const char * pathStart = paths;
    const char * pathEnd;
    while ((pathEnd = strchr(pathStart, SEPARATOR_CHAR)) != NULL)
    {
        size_t pathLen = pathEnd - pathStart;
        StringInitialize(&TBFilePaths[TBNumFilePaths]);
        StringSetN(&TBFilePaths[TBNumFilePaths], pathStart, pathLen);
        pathStart = pathEnd + 1;
        TBNumFilePaths++;
        assert(TBNumFilePaths <= numPaths);
    }

    // Handle last string as special case.
    if (*pathStart != '\0')
    {
        StringInitialize(&TBFilePaths[TBNumFilePaths]);
        StringSet(&TBFilePaths[TBNumFilePaths], pathStart);
        TBNumFilePaths++;
        assert(TBNumFilePaths <= numPaths);
    }

    // MapB1H1H7[] encodes a square below a1-h8 diagonal to 0..27
    int code = 0;
    for (Square s = SquareA1; s <= SquareH8; ++s)
    {
        if (off_A1H8(s) < 0)
            MapB1H1H7[s] = code++;
    }

    // MapA1D1D4[] encodes a square in the a1-d1-d4 triangle to 0..9
    Square diagonal[NUM_SQUARES];
    size_t diagonalLen = 0;
    code = 0;
    for (Square s = SquareA1; s <= SquareD4; ++s)
    {
        if (off_A1H8(s) < 0 && SquareGetFile(s) <= FileD)
            MapA1D1D4[s] = code++;
        else if (!off_A1H8(s) && SquareGetFile(s) <= FileD)
            diagonal[diagonalLen++] = s;
    }

    // Diagonal squares are encoded as last ones
    for (size_t i = 0; i < diagonalLen; ++i)
        MapA1D1D4[diagonal[i]] = code++;

    // MapKK[] encodes all the 462 possible legal positions of two kings where
    // the first is in the a1-d1-d4 triangle. If the first king is on the a1-d4
    // diagonal, the other one shall not to be above the a1-h8 diagonal.
    int * bothOnDiagonalIdx = (int *) malloc(17920 * sizeof(int)); // 10 * 28 * 64 = 17920
    if (bothOnDiagonalIdx == NULL)
        goto err;
    Square * bothOnDiagonalSq = (Square *) malloc(17920 * sizeof(Square)); // 10 * 28 * 64 = 17920
    if (bothOnDiagonalSq == NULL)
    {
        free(bothOnDiagonalIdx);
        goto err;
    }
    size_t bothOnDiagonalLen = 0;
    code = 0;
    for (int idx = 0; idx < 10; idx++)
    {
        for (Square s1 = SquareA1; s1 <= SquareD4; ++s1)
        {
            if (MapA1D1D4[s1] == idx && (idx || s1 == SquareB1)) // SquareB1 is mapped to 0
            {
                for (Square s2 = SquareA1; s2 <= SquareH8; ++s2)
                {
                    if ((GetKingMoves(s1) | s1) & s2)
                        continue; // Illegal position
                    else if (!off_A1H8(s1) && off_A1H8(s2) > 0)
                        continue; // First on diagonal, second above
                    else if (!off_A1H8(s1) && !off_A1H8(s2))
                    {
                        bothOnDiagonalIdx[bothOnDiagonalLen] = idx;
                        bothOnDiagonalSq[bothOnDiagonalLen] = s2;
                        bothOnDiagonalLen++;
                    }
                    else
                        MapKK[idx][s2] = code++;
                }
            }
        }
    }

    // Legal positions with both kings on diagonal are encoded as last ones
    for (size_t i = 0; i < bothOnDiagonalLen; ++i)
        MapKK[bothOnDiagonalIdx[i]][bothOnDiagonalSq[i]] = code++;

    free(bothOnDiagonalIdx);
    free(bothOnDiagonalSq);

    // Binomial[] stores the Binomial Coefficients using Pascal rule. There
    // are Binomial[k][n] ways to choose k elements from a set of n elements.
    Binomial[0][0] = 1;

    for (int n = 1; n < 64; n++) // Squares
    {
        for (int k = 0; k < 6 && k <= n; ++k) // Pieces
        {
            Binomial[k][n] = (k > 0 ? Binomial[k - 1][n - 1] : 0)
                           + (k < n ? Binomial[k][n - 1] : 0);
        }
    }

    // MapPawns[s] encodes squares a2-h7 to 0..47. This is the number of possible
    // available squares when the leading one is in 's'. Moreover the pawn with
    // highest MapPawns[] is the leading pawn, the one nearest the edge and,
    // among pawns with same file, the one with lowest rank.
    int availableSquares = 47; // Available squares when lead pawn is in a2

    // Init the tables for the encoding of leading pawns group: with 7-men TB we
    // can have up to 5 leading pawns (KPPPPPK).
    for (int leadPawnsCnt = 1; leadPawnsCnt <= 5; ++leadPawnsCnt)
    {
        for (File f = FileA; f <= FileD; ++f)
        {
            // Restart the index at every file because TB table is split
            // by file, so we can reuse the same index for different files.
            int idx = 0;

            // Sum all possible combinations for a given file, starting with
            // the leading pawn on rank 2 and increasing the rank.
            for (Rank r = Rank2; r <= Rank7; ++r)
            {
                Square sq = SquareFromRankFile(r, f);

                // Compute MapPawns[] at first pass.
                // If sq is the leading pawn square, any other pawn cannot be
                // below or more toward the edge of sq. There are 47 available
                // squares when sq = a2 and reduced by 2 for any rank increase
                // due to mirroring: sq == a3 -> no a2, h2, so MapPawns[a3] = 45
                if (leadPawnsCnt == 1)
                {
                    MapPawns[sq] = availableSquares--;
                    MapPawns[SQUARE_HORIZONTAL_FLIP(sq)] = availableSquares--;
                }
                LeadPawnIdx[leadPawnsCnt][sq] = idx;
                idx += Binomial[leadPawnsCnt - 1][MapPawns[sq]];
            }
            // After a file is traversed, store the cumulated per-file index
            LeadPawnsSize[leadPawnsCnt][f] = idx;
        }
    }

    // Add entries in TB tables if the corresponding ".rtbw" file exists
    for (PieceType p1 = Pawn; p1 < King; ++p1)
    {
        if (!TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, King }, 3))
            goto err;

        for (PieceType p2 = Pawn; p2 <= p1; ++p2)
        {
            if (!TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, p2, King }, 4))
                goto err;
            if (!TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, King, p2 }, 4))
                goto err;

            for (PieceType p3 = Pawn; p3 < King; ++p3)
            {
                if (!TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, p2, King, p3 }, 5))
                    goto err;
            }

            for (PieceType p3 = Pawn; p3 <= p2; ++p3)
            {
                if (!TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, p2, p3, King }, 5))
                    goto err;

                for (PieceType p4 = Pawn; p4 <= p3; ++p4)
                {
                    // Ignore errors here; 6-piece end tables are optional.
                    TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, p2, p3, p4, King }, 6);

                    for (PieceType p5 = Pawn; p5 <= p4; ++p5)
                    {
                        // Ignore errors here; 7-piece end tables are optional.
                        TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, p2, p3, p4, p5, King }, 7);
                    }

                    for (PieceType p5 = Pawn; p5 < King; ++p5)
                    {
                        // Ignore errors here; 7-piece end tables are optional.
                        TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, p2, p3, p4, King, p5 }, 7);
                    }
                }

                for (PieceType p4 = Pawn; p4 < King; ++p4)
                {
                    // Ignore errors here; 6-piece end tables are optional.
                    TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, p2, p3, King, p4 }, 6);

                    for (PieceType p5 = Pawn; p5 <= p4; ++p5)
                    {
                        // Ignore errors here; 7-piece end tables are optional.
                        TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, p2, p3, King, p4, p5 }, 7);
                    }
                }
            }

            for (PieceType p3 = Pawn; p3 <= p1; ++p3)
            {
                for (PieceType p4 = Pawn; p4 <= (p1 == p3 ? p2 : p3); ++p4)
                {
                    // Ignore errors here; 6-piece end tables are optional.
                    TBTablesAdd(&s_tbTables, (PieceType[]) { King, p1, p2, King, p3, p4 }, 6);
                }
            }
        }
    }

    //sync_cout << "info string Found " << TBTables.size() << " tablebases" << sync_endl;

    return true;

err:
    if (TBFilePaths != NULL)
    {
        for (size_t i = 0; i < TBNumFilePaths; ++i)
            StringDestroy(&TBFilePaths[i]);
        free(TBFilePaths);
        TBFilePaths = NULL;
    }
    TBNumFilePaths = 0;
    TBTablesDestroy(&s_tbTables);
    return false;
}

void SyzygyDestroy()
{
    TBTablesDestroy(&s_tbTables);
    if (s_mutexInitialized)
    {
        MutexDestroy(&s_mutex);
        s_mutexInitialized = false;
    }
    if (TBFilePaths != NULL)
    {
        for (size_t i = 0; i < TBNumFilePaths; ++i)
            StringDestroy(&TBFilePaths[i]);
        free(TBFilePaths);
        TBFilePaths = NULL;
    }
    TBNumFilePaths = 0;
}

// Probe the WDL table for a particular position.
// If *result != PROBE_STATE_FAIL, the probe was successful.
// The return value is from the point of view of the side to move:
// -2 : loss
// -1 : loss, but draw under 50-move rule
//  0 : draw
//  1 : win, but draw under 50-move rule
//  2 : win
WDLScore SyzygyProbeWDL(const Board * board, ProbeState * result)
{
    *result = PROBE_STATE_OK;
    return search(board, result, false);
}

// Probe the DTZ table for a particular position.
// If *result != PROBE_STATE_FAIL, the probe was successful.
// The return value is from the point of view of the side to move:
//         n < -100 : loss, but draw under 50-move rule
// -100 <= n < -1   : loss in n ply (assuming 50-move counter == 0)
//        -1        : loss, the side to move is mated
//         0        : draw
//     1 < n <= 100 : win in n ply (assuming 50-move counter == 0)
//   100 < n        : win, but draw under 50-move rule
//
// The return value n can be off by 1: a return value -n can mean a loss
// in n+1 ply and a return value +n can mean a win in n+1 ply. This
// cannot happen for tables with positions exactly on the "edge" of
// the 50-move rule.
//
// This implies that if dtz > 0 is returned, the position is certainly
// a win if dtz + 50-move-counter <= 99. Care must be taken that the engine
// picks moves that preserve dtz + 50-move-counter <= 99.
//
// If n = 100 immediately after a capture or pawn move, then the position
// is also certainly a win, and during the whole phase until the next
// capture or pawn move, the inequality to be preserved is
// dtz + 50-move-counter <= 100.
//
// In short, if a move is available resulting in dtz + 50-move-counter <= 99,
// then do not accept moves leading to dtz + 50-move-counter == 100.
int SyzygyProbeDTZ(const Board * board, ProbeState * result)
{
    *result = PROBE_STATE_OK;
    WDLScore wdl = search(board, result, true);

    if (*result == PROBE_STATE_FAIL || wdl == WDLDraw) // DTZ tables don't store draws
        return 0;

    // DTZ stores a 'don't care' value in this case, or even a plain wrong
    // one as in case the best move is a losing ep, so it cannot be probed.
    if (*result == PROBE_STATE_ZEROING_BEST_MOVE)
        return dtz_before_zeroing(wdl);

    int dtz = ProbeTable(board, result, wdl, TBTypeDTZ);

    if (*result == PROBE_STATE_FAIL)
        return 0;

    if (*result != PROBE_STATE_CHANGE_STM)
        return (dtz + 100 * (wdl == WDLBlessedLoss || wdl == WDLCursedWin)) * SignOf(wdl);

    // DTZ stores results for the other side, so we need to do a 1-ply search and
    // find the winning move that minimizes DTZ.
    int minDTZ = 0xFFFF;

    // TODO: Pass in moves from evaluation function so we don't need to eat up extra stack space here for no reason.
    Move moves[256];
    uint8_t numMoves = GetValidMoves(board, moves);
    Board nextBoard;
    unsigned long long numPiecesRemaining = intrinsic_popcnt64(board->allPieceTables);

    for (uint8_t i = 0; i < numMoves; ++i)
    {
        Move move = moves[i];
        nextBoard = *board;
        MakeMove(&nextBoard, move);

        bool zeroing = move.piece == Pawn || (numPiecesRemaining > intrinsic_popcnt64(nextBoard.allPieceTables)); // is pawn or is capture

        // For zeroing moves we want the dtz of the move _before_ doing it,
        // otherwise we will get the dtz of the next move sequence. Search the
        // position after the move to get the score sign (because even in a
        // winning position we could make a losing capture or going for a draw).
        dtz = zeroing ? -dtz_before_zeroing(search(&nextBoard, result, false))
                      : -SyzygyProbeDTZ(&nextBoard, result);

        // If the move mates, force minDTZ to 1
        if (dtz == 1 && IsCheckmate(&nextBoard))
            minDTZ = 1;

        // Convert result from 1-ply search. Zeroing moves are already accounted
        // by dtz_before_zeroing() that returns the DTZ of the previous move.
        if (!zeroing)
            dtz += SignOf(dtz);

        // Skip the draws and if we are winning only pick positive dtz
        if (dtz < minDTZ && SignOf(dtz) == SignOf(wdl))
            minDTZ = dtz;

        if (*result == PROBE_STATE_FAIL)
            return 0;
    }

    // When there are no legal moves, the position is mate: we return -1
    return minDTZ == 0xFFFF ? -1 : minDTZ;
}

#if 0
// Use the DTZ tables to rank root moves.
//
// A return value false indicates that not all probes were successful.
bool SyzygyRootProbe(const Board * board, const Move * rootMoves, uint8_t numMoves)
{
    Board nextBoard;
    ProbeState result = PROBE_STATE_OK;

    // Obtain 50-move counter for the root position
    int cnt50 = board->halfmoveCounter;

    // Check whether a position was repeated since the last zeroing move.
    bool rep = pos.has_repeated();

    int dtz;
    int bound = MAX_DTZ - 100;
    //int dtz, bound = Options["Syzygy50MoveRule"] ? (MAX_DTZ - 100) : 1;

    // Probe and rank each move
    for (uint8_t i = 0; i < numMoves; ++i)
    {
        Move move = rootMoves[i];
        nextBoard = *board;
        MakeMove(&nextBoard, move);
        //pos.do_move(m.pv[0], st);

        // Calculate dtz for the current move counting from the root position
        if (board->halfmoveCounter == 0)
        {
            // In case of a zeroing move, dtz is one of -101/-1/0/1/101
            WDLScore wdl = -SyzygyProbeWDL(&nextBoard, &result);
            dtz = dtz_before_zeroing(wdl);
        }
        else if (pos.is_draw(1))
        {
            // In case a root move leads to a draw by repetition or
            // 50-move rule, we set dtz to zero. Note: since we are
            // only 1 ply from the root, this must be a true 3-fold
            // repetition inside the game history.
            dtz = 0;
        }
        else
        {
            // Otherwise, take dtz for the new position and correct by 1 ply
            dtz = -SyzygyProbeDTZ(&nextBoard, &result);
            dtz =  dtz > 0 ? dtz + 1
                 : dtz < 0 ? dtz - 1 : dtz;
        }

        // Make sure that a mating move is assigned a dtz value of 1
        if (dtz == 2 && IsCheckmate(&nextBoard))
            dtz = 1;

        //pos.undo_move(m.pv[0]);

        if (result == PROBE_STATE_FAIL)
            return false;

        // Better moves are ranked higher. Certain wins are ranked equally.
        // Losing moves are ranked equally unless a 50-move draw is in sight.
        int r =  dtz > 0 ? (dtz + cnt50 <= 99 && !rep ? MAX_DTZ : MAX_DTZ - (dtz + cnt50))
               : dtz < 0 ? (-dtz * 2 + cnt50 < 100 ? -MAX_DTZ : -MAX_DTZ + (-dtz + cnt50))
               : 0;

// TODO: For this to work, need to implement storage for score and rank per move.
#if 0
        m.tbRank = r;

        // Determine the score to be displayed for this move. Assign at least
        // 1 cp to cursed wins and let it grow to 49 cp as the positions gets
        // closer to a real win.
        m.tbScore =  r >= bound ? CHECKMATE_WIN - EVAL_MAX_CHECKMATE_PLY - 1
                   : r >  0     ? Value((max( 3, r - (MAX_DTZ - 200)) * PAWN_VALUE_EG) / 200)
                   : r == 0     ? 0 // VALUE_DRAW
                   : r > -bound ? Value((min(-3, r + (MAX_DTZ - 200)) * PAWN_VALUE_EG) / 200)
                   :              CHECKMATE_LOSE + EVAL_MAX_CHECKMATE_PLY + 1;
#endif
    }

    return true;
}


// Use the WDL tables to rank root moves.
// This is a fallback for the case that some or all DTZ tables are missing.
//
// A return value false indicates that not all probes were successful.
bool SyzygyRootProbeWDL(const Board * board, const Move * rootMoves, uint8_t numMoves)
{
    static const int WDL_to_rank[] =
    {
        -MAX_DTZ,
        -MAX_DTZ + 101,
        0,
        MAX_DTZ - 101,
        MAX_DTZ
    };

    Board nextBoard;
    ProbeState result = PROBE_STATE_OK;
    WDLScore wdl;

#if 0
    const bool rule50 = true; // Options["Syzygy50MoveRule"];
#endif

    // Probe and rank each move
    for (uint8_t i = 0; i < numMoves; ++i)
    {
        Move move = rootMoves[i];
        nextBoard = *board;
        MakeMove(&nextBoard, move);

        //pos.do_move(m.pv[0], st);

        if (pos.is_draw(1))
            wdl = WDLDraw;
        else
            wdl = -SyzygyProbeWDL(&nextBoard, &result);

        //pos.undo_move(m.pv[0]);

        if (result == PROBE_STATE_FAIL)
            return false;

// TODO: For this to work, need to implement storage for score and rank per move.
#if 0
        m.tbRank = WDL_to_rank[wdl + 2];

        if (!rule50)
        {
            wdl = wdl > WDLDraw ? WDLWin :
                  wdl < WDLDraw ? WDLLoss : WDLDraw;
        }
        m.tbScore = WDL_to_value[wdl + 2];
#endif
    }

    return true;
}
#endif // Root probe disabled
