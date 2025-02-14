#ifndef MEMORY_MAPPED_FILE_H_
#define MEMORY_MAPPED_FILE_H_

#include "StringStruct.h"
#include "WindowsInclude.h"

#include <stdbool.h>

typedef struct MemoryMappedFile
{
    void * baseAddress;
    uint64_t length;
#ifdef _MSC_VER
    HANDLE handle;
#endif

} MemoryMappedFile;

extern void MemoryMappedFileInitialize(MemoryMappedFile * file);
extern bool MemoryMappedFileInitializeAndOpen(MemoryMappedFile * file, const char * filename);
extern void MemoryMappedFileDestroy(MemoryMappedFile * file);
extern bool MemoryMappedFileOpen(MemoryMappedFile * file, const char * filename);
extern void MemoryMappedFileClose(MemoryMappedFile * file);

static inline uint64_t MemoryMappedFileGetSize(MemoryMappedFile * file)
{
    return file->length;
}

static inline bool MemoryMappedFileIsOpen(const MemoryMappedFile * file)
{
    return file->baseAddress != NULL;
}

static inline void * MemoryMappedFileGetAddress(MemoryMappedFile * file)
{
    return file->baseAddress;
}

#endif // MEMORY_MAPPED_FILE_H_
