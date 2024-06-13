#pragma once

#include <cstdint>  // for uint8_t, uint32_t, uint64_t
#include <cmath>

#include "hcl_utils.h"  // for VERIFY

#define LOG_ARRAY_NUM_DIGITS 4
#define LOG_ARRAY_SIZE       (1 << LOG_ARRAY_NUM_DIGITS)

inline bool isPowerOf2(const uint64_t num)
{
    return __builtin_popcountll(num) == 1;
}

inline uint64_t mod(uint64_t num, uint64_t divider)
{
    uint64_t dividerMinusOne = divider - 1;
    if (likely(isPowerOf2(divider)))
    {
        return num & dividerMinusOne;
    }
    return num % divider;
}

inline uint64_t div(uint64_t num, uint64_t divider)
{
    if (likely(isPowerOf2(divider)))
    {
        return num >> __builtin_ctzll(divider);
    }
    return num / divider;
}

inline uint32_t div(uint32_t num, uint32_t divider)
{
    if (likely(isPowerOf2((uint64_t)divider)))
    {
        return num >> __builtin_ctz(divider);
    }
    return num / divider;
}

inline uint64_t round_down(const uint64_t value, const uint64_t alignment)
{
    return div(value, alignment) * alignment;
}

inline uint64_t div_round_up(const uint64_t a, const uint64_t b)
{
    VERIFY(b != 0, "Divider should be non zero, a={}", a);
    return div(a + b - 1, b);
}

inline uint64_t round_to_multiple(const uint64_t a, const uint64_t mul)
{
    return mul == 0 ? 0 : mul * div_round_up(a, mul);
}

inline uint8_t* round_to_multiple(const uint8_t* a, const uint64_t mul)
{
    uint64_t rtn = round_to_multiple((uint64_t)a, mul);
    return (uint8_t*)rtn;
}

#define PLUS_PLUS(INDEX, MAX_SIZE) INDEX = (INDEX + 1) % MAX_SIZE;