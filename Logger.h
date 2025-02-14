#ifndef LOGGER_H_
#define LOGGER_H_

#include <stdbool.h>

extern bool LoggerInit(const char * filename);
extern void LoggerDestroy();
extern bool LoggerSetFilename(const char * filename);
extern const char * LoggerGetFilename();
extern void LoggerEnable(bool enable);
extern void LoggerLog(const char * str);
extern void LoggerLogLine(const char * str);
extern void LoggerLogf(const char * format, ...);
extern void LoggerLogLinef(const char * format, ...);
extern void LoggerFlush();

#endif // LOGGER_H_
