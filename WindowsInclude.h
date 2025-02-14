#ifndef WINDOWS_INCLUDE_H_
#define WINDOWS_INCLUDE_H_

#ifdef _MSC_VER
#define MICROSOFT_WINDOWS_WINBASE_H_DEFINE_INTERLOCKED_CPLUSPLUS_OVERLOADS 0
#include <Windows.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif
#endif

#endif // WINDOWS_INCLUDE_H_
