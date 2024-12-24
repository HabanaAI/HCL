#pragma once

#include <cstdint>

#define SET_FIELD(field, value)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        (field) = (value);                                                                                             \
        VERIFY((field) == (value), "The values 0x{:x},0x{:x} are not equal.", field, value);                           \
    } while (0);
