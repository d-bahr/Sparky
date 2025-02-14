#ifndef STATIC_ASSERT_H_
#define STATIC_ASSERT_H_

#if __STDC_VERSION__ >= 202300
#define STATIC_ASSERT(x, msg) static_assert((x), msg)
#else
#include <assert.h>
#define STATIC_ASSERT(x, msg) _Static_assert((x), msg)
#endif

#endif // STATIC_ASSERT_H_
