#include "Logger.h"
#include "Mutex.h"

#include <stdarg.h>
#include <stdio.h>

static Mutex s_mutex;
static const char * s_filename = NULL;
static FILE * s_logFile = NULL;
static bool s_enabled = true;

static bool OpenLogFile(const char * file)
{
    // Mutex lock is assumed to be already held.
    if (s_logFile != NULL)
        return false;
    s_logFile = fopen(file, "w");
    return s_logFile != NULL;
}

static void CloseLogFile()
{
    // Mutex lock is assumed to be already held.
    if (s_logFile != NULL)
    {
        fclose(s_logFile);
        s_logFile = NULL;
    }
}

bool LoggerInit(const char * filename)
{
    if (!MutexInitialize(&s_mutex))
        return false;

    return LoggerSetFilename(filename);
}

void LoggerDestroy()
{
    MutexLock(&s_mutex);
    CloseLogFile();
    MutexUnlock(&s_mutex);
    MutexDestroy(&s_mutex);
}

bool LoggerSetFilename(const char * filename)
{
    bool success = true;
    MutexLock(&s_mutex);
    if (s_filename != filename)
    {
        CloseLogFile();
        s_filename = filename;
        if (s_filename != NULL)
            success = OpenLogFile(s_filename);
    }
    MutexUnlock(&s_mutex);
    return success;
}

const char * LoggerGetFilename()
{
    return s_filename;
}

void LoggerEnable(bool enable)
{
    MutexLock(&s_mutex);
    s_enabled = enable;
    MutexUnlock(&s_mutex);
}

static void LogStr(bool newline, const char * str)
{
    MutexLock(&s_mutex);
    if (s_enabled && s_logFile != NULL)
    {
        fputs(str, s_logFile);
        if (newline)
        {
            fputs("\n", s_logFile);
            fflush(s_logFile);
        }
    }
    MutexUnlock(&s_mutex);
}

static void LogStrf(bool newline, const char * format, va_list args)
{
    MutexLock(&s_mutex);
    if (s_enabled && s_logFile != NULL)
    {
        vfprintf(s_logFile, format, args);
        if (newline)
        {
            fputc('\n', s_logFile);
            fflush(s_logFile);
        }
    }
    MutexUnlock(&s_mutex);
}

void LoggerLog(const char * str)
{
    LogStr(false, str);
}

void LoggerLogLine(const char * str)
{
    LogStr(true, str);
}

void LoggerLogf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    LogStrf(false, format, args);
}

void LoggerLogLinef(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    LogStrf(true, format, args);
}

void LoggerFlush()
{
    MutexLock(&s_mutex);
    if (s_logFile != NULL)
        fflush(s_logFile);
    MutexUnlock(&s_mutex);
}
