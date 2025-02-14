#ifndef INTRINSICS_H_
#define INTRINSICS_H_

#ifdef _MSC_VER
#include <intrin.h>
#elif defined(__GNUC__)
#include <x86intrin.h>
#endif

#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__)
#define FORCE_INLINE inline __attribute__((always_inline))
#else
#define FORCE_INLINE inline
#endif

#include <stdbool.h>
#include <stdint.h>

// This function returns the index of the highest set bit in a 64 bit integer (output value between 0 and 63).
static inline uint8_t msbDeBruijn64(uint64_t value)
{
    static const uint8_t tab64[64] =
    {
        63,  0, 58,  1, 59, 47, 53,  2,
        60, 39, 48, 27, 54, 33, 42,  3,
        61, 51, 37, 40, 49, 18, 28, 20,
        55, 30, 34, 11, 43, 14, 22,  4,
        62, 57, 46, 52, 38, 26, 32, 41,
        50, 36, 17, 19, 29, 10, 13, 21,
        56, 45, 25, 31, 35, 16,  9, 12,
        44, 24, 15,  8, 23,  7,  6,  5
    };

    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    return tab64[((uint64_t) ((value - (value >> 1ull)) * 0x07EDD5E59A4E28C2ull)) >> 58ull];
}

// This function returns the index of the lowest set bit in a 64 bit integer (output value between 0 and 63).
static inline uint8_t lsbDeBruijn64(uint64_t value)
{
    static const uint8_t tab64[64] =
    {
         0, 47,  1, 56, 48, 27,  2, 60,
        57, 49, 41, 37, 28, 16,  3, 61,
        54, 58, 35, 52, 50, 42, 21, 44,
        38, 32, 29, 23, 17, 11,  4, 62,
        46, 55, 26, 59, 40, 36, 15, 53,
        34, 51, 20, 43, 31, 22, 10, 45,
        25, 39, 14, 33, 19, 30,  9, 24,
        13, 18,  8, 12,  7,  6,  5, 63
    };

    return tab64[((uint64_t) ((value ^ (value - 1ull)) * 0x03f79d71b4cb0a89ull)) >> 58ull];
}

#if defined(_MSC_VER)
static FORCE_INLINE unsigned long intrinsic_bsr64(unsigned long long x)
{
    unsigned long idx = 0;
    _BitScanReverse64(&idx, x);
    return idx;
}
#elif defined(__GNUC__)
static FORCE_INLINE int intrinsic_bsr64(uint64_t x)
{
    return 63 - __builtin_clzll(x);
}
#else
#define intrinsic_bsr64 msbDeBruijn64
#endif

#if defined(_MSC_VER)
static FORCE_INLINE unsigned long intrinsic_bsf64(unsigned long long x)
{
    unsigned long idx = 0;
    _BitScanForward64(&idx, x);
    return idx;
}
#elif defined(__GNUC__)
static FORCE_INLINE int intrinsic_bsf64(uint64_t x)
{
    return __builtin_ctzll(x);
}
#else
#define intrinsic_bsf64 lsbDeBruijn64
#endif

#if defined(_MSC_VER)
static FORCE_INLINE unsigned long long intrinsic_popcnt64(unsigned long long x)
{
    return __popcnt64(x);
}
#elif defined(__GNUC__)
static FORCE_INLINE int intrinsic_popcnt64(uint64_t x)
{
    return __builtin_popcountll(x);
}
#else
#error Platform not supported (missing popcnt instrinsic)
#endif

static FORCE_INLINE unsigned long long intrinsic_pext64(unsigned long long x, unsigned long long mask)
{
#if defined(_MSC_VER) || defined(__GNUC__)
    return _pext_u64(x, mask);
#else
#error Platform not supported (missing pext64 instrinsic)
#endif
}

static FORCE_INLINE unsigned long long intrinsic_pdep64(unsigned long long x, unsigned long long mask)
{
#if defined(_MSC_VER) || defined(__GNUC__)
    return _pdep_u64(x, mask);
#else
#error Platform not supported (missing pext64 instrinsic)
#endif
}

static FORCE_INLINE unsigned long long intrinsic_blsr64(unsigned long long x)
{
#if defined(_MSC_VER) || defined(__GNUC__)
    return _blsr_u64(x);
#else
#error Platform not supported (missing pext64 instrinsic)
#endif
}

// Note: x is the "not-ed" variable; i.e.:
// ~x & y
static FORCE_INLINE unsigned long long intrinsic_andn64(unsigned long long x, unsigned long long y)
{
#if defined(_MSC_VER)
    return _andn_u64(x, y);
#elif defined(__GNUC__)
    return __andn_u64(x, y);
#else
#error Platform not supported (missing pext64 instrinsic)
#endif
}

static FORCE_INLINE unsigned short intrinsic_bswap16(unsigned short x)
{
#if defined(_MSC_VER)
    return _byteswap_ushort(x);
#elif defined(__GNUC__)
    return __builtin_bswap16(x);
#else
#error Platform not supported (missing byteswap instrinsic)
#endif
}

static FORCE_INLINE unsigned long intrinsic_bswap32(unsigned long x)
{
#if defined(_MSC_VER)
    return _byteswap_ulong(x);
#elif defined(__GNUC__)
    return __builtin_bswap32(x);
#else
#error Platform not supported (missing byteswap instrinsic)
#endif
}

static FORCE_INLINE unsigned long long intrinsic_bswap64(unsigned long long x)
{
#if defined(_MSC_VER)
    return _byteswap_uint64(x);
#elif defined(__GNUC__)
    return __builtin_bswap64(x);
#else
#error Platform not supported (missing byteswap instrinsic)
#endif
}

#endif // INTRINSICS_H_
