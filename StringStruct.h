#ifndef STRING_STRUCT_H_
#define STRING_STRUCT_H_

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __GNUC__
#include <strings.h>
#endif

#define STRING_NOT_FOUND ((size_t)(-1))

typedef struct
{
    char * chars;
    size_t length;
    size_t maxLength;
} String;

static inline bool StringInitialize(String * str)
{
    str->length = 0;
    str->maxLength = 64;
    str->chars = (char *) malloc(str->maxLength + 1);
    if (str->chars == NULL)
        return false;
    memset(&str->chars[str->length], 0, str->maxLength + 1);
    return true;
}

static inline void StringDestroy(String * string)
{
    free(string->chars);
    string->length = 0;
}

static inline bool StringTryExpand(String * str, size_t desiredLength)
{
    if (str->maxLength < desiredLength)
    {
        // Expand current memory.
        size_t doubling = str->maxLength * 2;
        size_t newMaxLength = (desiredLength > doubling ? desiredLength : doubling) + 1;
        char * newChars = (char *) malloc(newMaxLength);
        if (!newChars)
            return false;
        memcpy(newChars, str->chars, str->length);
        memset(&newChars[str->length], 0, newMaxLength - str->length);
        free(str->chars);
        str->chars = newChars;
    }

    return true;
}

static inline void StringClear(String * str)
{
    memset(str->chars, 0, str->length);
    str->length = 0;
}

static inline size_t StringLength(const String * str)
{
    return str->length;
}

static inline size_t StringMaxLength(const String * str)
{
    return str->maxLength;
}

static inline bool StringReserve(String * str, size_t reserve)
{
    return StringTryExpand(str, reserve);
}

static inline bool StringAppendN(String * str, const char * chars, size_t len)
{
    if (!StringTryExpand(str, str->length + len))
        return false;

    memcpy(&str->chars[str->length], chars, len);
    str->length += len;
    return true;
}

static inline bool StringAppend(String * str, const char * chars)
{
    size_t len = strlen(chars);
    if (len == 0)
        return true;

    return StringAppendN(str, chars, len);
}

static inline bool StringPush(String * str, char c)
{
    if (!StringTryExpand(str, str->length + 1))
        return false;

    str->chars[str->length++] = c;
    return true;
}

static inline void StringPop(String * str)
{
    if (str->length > 0)
    {
        str->length--;
        str->chars[str->length] = 0;
    }
}

static inline bool StringSetN(String * str, const char * chars, size_t length)
{
    if (str->length < length)
    {
        if (!StringTryExpand(str, length))
            return false;

        memcpy(str->chars, chars, length);
        str->length = length;
    }
    else
    {
        memcpy(str->chars, chars, length);
        memset(&str->chars[length], 0, str->length - length);
        str->length = length;
    }

    return true;
}

static inline bool StringSet(String * str, const char * chars)
{
    size_t len = strlen(chars);
    return StringSetN(str, chars, len);
}

static inline bool StringConcat(String * str, const String * append)
{
    if (!StringTryExpand(str, str->length + append->length))
        return false;

    memcpy(&str->chars[str->length], append->chars, append->length);
    str->length += append->length;
    return true;
}

static inline bool StringInsert(String * str, size_t index, char c)
{
    if (!StringTryExpand(str, str->length + 1))
        return false;

    if (index < str->length)
    {
        memmove(&str->chars[index + 1], &str->chars[index], str->length - index);
        str->chars[index] = c;
        str->length++;
    }
    else
    {
        str->chars[str->length++] = c;
    }

    return true;
}

static inline const char * StringGetChars(const String * str)
{
    return str->chars;
}

static inline char * StringGetCharsMutable(String * str)
{
    return str->chars;
}

static inline char StringCharAt(const String * str, size_t index)
{
    if (index >= str->length)
        return 0;
    else
        return str->chars[index];
}

static inline void StringSub(const String * str, size_t start, size_t length, String * sub)
{
    if (start >= str->length)
    {
        StringClear(sub);
        return;
    }

    if (start + length > str->length)
        length = str->length - start;

    StringSetN(sub, &str->chars[start], length);
}

static inline size_t StringFindFromIndex(const String * str, char c, size_t start)
{
    for (size_t i = start; i < str->length; ++i)
    {
        if (str->chars[i] == c)
            return i;
    }
    return STRING_NOT_FOUND;
}

static inline size_t StringFind(const String * str, char c)
{
    return StringFindFromIndex(str, c, 0);
}

static inline void StringToLowerInPlace(String * str)
{
    for (size_t i = 0; i < str->length; ++i)
        str->chars[i] = tolower(str->chars[i]);
}

static inline bool StringCopy(String * dest, const String * src)
{
    return StringSetN(dest, src->chars, src->length);
}

static inline int StringCompareString(const String * a, const String * b)
{
    return strcmp(a->chars, b->chars);
}

static inline int StringCompareN(const String * a, const char * b, size_t length)
{
    return strncmp(a->chars, b, length);
}

static inline int StringCompare(const String * a, const char * b)
{
    return strcmp(a->chars, b);
}

static inline int StringICompareString(const String * a, const String * b)
{
#ifdef _MSC_VER
    return _stricmp(a->chars, b->chars);
#else
    return strcasecmp(a->chars, b->chars);
#endif
}

static inline int StringICompareN(const String * a, const char * b, size_t length)
{
#ifdef _MSC_VER
    return _strnicmp(a->chars, b, length);
#else
    return strncasecmp(a->chars, b, length);
#endif
}

static inline int StringICompare(const String * a, const char * b)
{
#ifdef _MSC_VER
    return _stricmp(a->chars, b);
#else
    return strcasecmp(a->chars, b);
#endif
}

static inline bool StringEqualsString(const String * a, const String * b)
{
    return (a->length == b->length) && (memcmp(a->chars, b->chars, a->length) == 0);
}

static inline bool StringEqualsN(const String * a, const char * b, size_t length)
{
    return (a->length == length) && (memcmp(a->chars, b, length) == 0);
}

static inline bool StringEquals(const String * a, const char * b)
{
    return StringEqualsN(a, b, strlen(b));
}

static inline bool StringIEqualsString(const String * a, const String * b)
{
    return (a->length == b->length) && (StringICompareString(a, b) == 0);
}

static inline bool StringIEqualsN(const String * a, const char * b, size_t length)
{
    return (a->length == length) && (StringICompareN(a, b, length) == 0);
}

static inline bool StringIEquals(const String * a, const char * b)
{
    return StringIEqualsN(a, b, strlen(b));
}

#endif // STRING_STRUCT_H_
