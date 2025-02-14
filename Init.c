#include "Init.h"

#include "tables/MoveTables.h"

#ifdef __GNUC__
#include <alloca.h>
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(WIN32)
#include "WindowsInclude.h"
#define FILE_PATH_SEPARATOR "\\"
#define FILE_PATH_SEPARATOR_CHAR '\\'
#else
#include <linux/limits.h>
#include <unistd.h>
#define FILE_PATH_SEPARATOR "/"
#define FILE_PATH_SEPARATOR_CHAR '/'
#endif

static const char * GetExecutableDirectory()
{
    static char * s_execDir = NULL;
    if (s_execDir == NULL)
    {
#if defined(_WIN32) || defined(WIN32)
        static char buf[MAX_PATH + 1];
        memset(buf, 0, sizeof(buf));
        GetModuleFileNameA(NULL, buf, sizeof(buf));
        s_execDir = buf;
#else
        static char buf[PATH_MAX + 1];
        memset(buf, 0, sizeof(buf));
        ssize_t size = readlink("/proc/self/exe", buf, sizeof(buf));
        if (size == -1)
            return "";
        s_execDir = buf;
#endif
        char * k = strrchr(s_execDir, FILE_PATH_SEPARATOR_CHAR);
        if (k != NULL)
            *k = '\0';
    }
    return s_execDir;
}

static int LoadRookBlockerBitboards(const char * bitboardsDir)
{
    static const char * const rookBlockerBitboardsFilename = "rookBlockerBitboards.bin";
    size_t filePathLen = strlen(bitboardsDir) + strlen(rookBlockerBitboardsFilename) + 2; // +2 is for path separator and null terminator
    char * rookBlockerBitboardsPath = (char *) alloca(filePathLen);
    int z = snprintf(rookBlockerBitboardsPath, filePathLen, "%s" FILE_PATH_SEPARATOR "%s", bitboardsDir, rookBlockerBitboardsFilename);
    if (z != filePathLen - 1)
        return 1;

    FILE * rookBlockerBitboardsFile = fopen(rookBlockerBitboardsPath, "rb");
    if (rookBlockerBitboardsFile == NULL)
        return 1;

    fseek(rookBlockerBitboardsFile, 0, SEEK_END);
    long len = ftell(rookBlockerBitboardsFile);
    if (len % sizeof(uint64_t) != 0)
        return 1;
    rewind(rookBlockerBitboardsFile);
    s_rookBlockerBitboardsContiguous = (uint64_t *) malloc(len);
    if (!s_rookBlockerBitboardsContiguous)
        return 1;
    if (sizeof(uint64_t) != fread(s_rookBlockerBitboardsContiguous, len >> 3, sizeof(uint64_t), rookBlockerBitboardsFile))
        return 1;
    fclose(rookBlockerBitboardsFile);

    static const uint64_t MagicBitboardLenPerSquare[NUM_SQUARES] =
    {
        12, 11, 11, 11, 11, 11, 11, 12, // Rank 1
        11, 10, 10, 10, 10, 10, 10, 11, // Rank 2
        11, 10, 10, 10, 10, 10, 10, 11, // Rank 3
        11, 10, 10, 10, 10, 10, 10, 11, // Rank 4
        11, 10, 10, 10, 10, 10, 10, 11, // Rank 5
        11, 10, 10, 10, 10, 10, 10, 11, // Rank 6
        11, 10, 10, 10, 10, 10, 10, 11, // Rank 7
        12, 11, 11, 11, 11, 11, 11, 12  // Rank 8
    };

    s_rookBlockerBitboards[0] = s_rookBlockerBitboardsContiguous;
    for (int i = 1; i < NUM_SQUARES; ++i)
    {
        s_rookBlockerBitboards[i] = s_rookBlockerBitboards[i - 1] + (1ull << MagicBitboardLenPerSquare[i - 1]);
        assert(s_rookBlockerBitboards[i] < s_rookBlockerBitboardsContiguous + (len >> 3));
    }

    return 0;
}

static int LoadBishopBlockerBitboards(const char * bitboardsDir)
{
    static const char * const bishopBlockerBitboardsFilename = "bishopBlockerBitboards.bin";
    size_t filePathLen = strlen(bitboardsDir) + strlen(bishopBlockerBitboardsFilename) + 2; // +2 is for path separator and null terminator
    char * bishopBlockerBitboardsPath = (char *) alloca(filePathLen);
    if (snprintf(bishopBlockerBitboardsPath, filePathLen, "%s" FILE_PATH_SEPARATOR "%s", bitboardsDir, bishopBlockerBitboardsFilename) != filePathLen - 1)
        return 1;

    FILE * bishopBlockerBitboardsFile = fopen(bishopBlockerBitboardsPath, "rb");
    if (bishopBlockerBitboardsFile == NULL)
        return 1;

    fseek(bishopBlockerBitboardsFile, 0, SEEK_END);
    long len = ftell(bishopBlockerBitboardsFile);
    if (len % sizeof(uint64_t) != 0)
        return 1;
    rewind(bishopBlockerBitboardsFile);
    s_bishopBlockerBitboardsContiguous = (uint64_t *) malloc(len);
    if (!s_bishopBlockerBitboardsContiguous)
        return 1;
    if (sizeof(uint64_t) != fread(s_bishopBlockerBitboardsContiguous, len >> 3, sizeof(uint64_t), bishopBlockerBitboardsFile))
        return 1;
    fclose(bishopBlockerBitboardsFile);

    static const uint64_t MagicBitboardLenPerSquare[NUM_SQUARES] =
    {
        6, 5, 5, 5, 5, 5, 5, 6, // Rank 1
        5, 5, 5, 5, 5, 5, 5, 5, // Rank 2
        5, 5, 7, 7, 7, 7, 5, 5, // Rank 3
        5, 5, 7, 9, 9, 7, 5, 5, // Rank 4
        5, 5, 7, 9, 9, 7, 5, 5, // Rank 5
        5, 5, 7, 7, 7, 7, 5, 5, // Rank 6
        5, 5, 5, 5, 5, 5, 5, 5, // Rank 7
        6, 5, 5, 5, 5, 5, 5, 6  // Rank 8
    };

    s_bishopBlockerBitboards[0] = s_bishopBlockerBitboardsContiguous;
    for (int i = 1; i < NUM_SQUARES; ++i)
    {
        s_bishopBlockerBitboards[i] = s_bishopBlockerBitboards[i - 1] + (1ull << MagicBitboardLenPerSquare[i - 1]);
        assert(s_bishopBlockerBitboards[i] < s_bishopBlockerBitboardsContiguous + (len >> 3));
    }

    return 0;
}

int Init(const char * bitboardsDir)
{
    const char * dir = (bitboardsDir != NULL) ? bitboardsDir : GetExecutableDirectory();

    int result = LoadRookBlockerBitboards(dir);
    if (result != 0)
        return result;

    return LoadBishopBlockerBitboards(dir);
}

void Cleanup()
{
    if (s_bishopBlockerBitboardsContiguous)
    {
        free(s_bishopBlockerBitboardsContiguous);
        s_bishopBlockerBitboardsContiguous = 0;
    }

    if (s_rookBlockerBitboardsContiguous)
    {
        free(s_rookBlockerBitboardsContiguous);
        s_rookBlockerBitboardsContiguous = 0;
    }
}
