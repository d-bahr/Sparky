#ifndef SORT_H_
#define SORT_H_

#include <stdbool.h>
#include <stddef.h>

typedef bool(*SortFunc)(void * a, void * b);

extern void StableSort(void * start, void * end, size_t objLen, SortFunc f);

#endif // SORT_H_
