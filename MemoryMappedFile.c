#include "MemoryMappedFile.h"

#include <stdio.h>

#ifdef __GNUC__
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

void MemoryMappedFileInitialize(MemoryMappedFile * file)
{
    file->baseAddress = NULL;
#ifndef _MSC_VER
    file->length = 0;
#endif
}

bool MemoryMappedFileInitializeAndOpen(MemoryMappedFile * file, const char * filename)
{
    MemoryMappedFileInitialize(file);
    return MemoryMappedFileOpen(file, filename);
}

void MemoryMappedFileDestroy(MemoryMappedFile * file)
{
    MemoryMappedFileClose(file);
}

bool MemoryMappedFileOpen(MemoryMappedFile * file, const char * filename)
{
    if (file->baseAddress != NULL)
        return true; // Already open.

#ifndef _WIN32
    struct stat statbuf;
    int fd = open(filename, O_RDONLY);

    if (fd == -1)
        return false;

    fstat(fd, &statbuf);

    file->length = statbuf.st_size;
    file->baseAddress = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
#if defined(MADV_RANDOM)
    madvise(file->baseAddress, statbuf.st_size, MADV_RANDOM);
#endif
    close(fd);

    if (file->baseAddress == MAP_FAILED)
    {
        fprintf(stderr, "Could not mmap() %s\n", filename);
        file->baseAddress = NULL;
        file->length = 0;
        return false;
    }
#else
    // Note FILE_FLAG_RANDOM_ACCESS is only a hint to Windows and as such may get ignored.
    HANDLE fd = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);

    if (fd == INVALID_HANDLE_VALUE || fd == NULL)
        return false;

    LARGE_INTEGER size;
    if (!GetFileSizeEx(fd, &size))
    {
        CloseHandle(fd);
        return false;
    }

    file->handle = CreateFileMapping(fd, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(fd);

    if (file->handle == INVALID_HANDLE_VALUE || file->handle == NULL)
    {
        fprintf(stderr, "CreateFileMapping() failed\n");
        return false;
    }

    file->baseAddress = MapViewOfFile(file->handle, FILE_MAP_READ, 0, 0, 0);

    if (file->baseAddress == NULL)
    {
        fprintf(stderr, "MapViewOfFile() failed, name = %s, error = %d\n", filename, GetLastError());
        CloseHandle(file->handle);
        file->handle = INVALID_HANDLE_VALUE;
        return false;
    }

    file->length = size.QuadPart;
#endif

    return true;
}

void MemoryMappedFileClose(MemoryMappedFile * file)
{
    if (file->baseAddress != NULL)
    {
#ifndef _MSC_VER
        munmap(file->baseAddress, file->length);
#else
        UnmapViewOfFile(file->baseAddress);
        CloseHandle(file->handle);
        file->handle = INVALID_HANDLE_VALUE;
#endif
        file->baseAddress = NULL;
        file->length = 0;
    }
}
