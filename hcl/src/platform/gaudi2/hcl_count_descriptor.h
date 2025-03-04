#pragma once

#include <cstdint>

#include "hcl_utils.h"       // for UNUSED
#include "hcl_math_utils.h"  // for div_round_up

class CountDescriptor
{
public:
    uint32_t m_cacheLineCount;
    uint32_t m_cacheLineRemainder;
    uint8_t  m_elementRemainder;

    CountDescriptor(uint64_t cellCount, unsigned numNicsInUse);

    bool isShort() const;

private:
    uint32_t m_cellCount;
};

inline CountDescriptor::CountDescriptor(uint64_t cellCount, unsigned numNicsInUse) : m_cellCount(cellCount)
{
    UNUSED(m_cellCount);
    uint64_t totalCacheLines =
        div_round_up(cellCount, NIC_CACHE_LINE_SIZE_IN_ELEMENTS);    // number of cache-lines each rank needs to send
    m_cacheLineCount = div_round_up(totalCacheLines, numNicsInUse);  // number of cache-lines each nic needs to send
    m_cacheLineRemainder =
        (m_cacheLineCount * numNicsInUse) == totalCacheLines
            ? 0
            : (m_cacheLineCount * numNicsInUse) - totalCacheLines;  // used by the last nic to calculate how much data
                                                                    // it needs to send (if less then the other 2)
    m_elementRemainder = (totalCacheLines * NIC_CACHE_LINE_SIZE_IN_ELEMENTS) == cellCount
                             ? 0
                             : (totalCacheLines * NIC_CACHE_LINE_SIZE_IN_ELEMENTS) -
                                   cellCount;  // the size the last nic needs to send, that is smaller than a cache-line
}

inline bool CountDescriptor::isShort() const
{
    static constexpr uint64_t MAX_CACHE_LINES_IN_SHORT_COMMAND =
        ((1 << 13) - 1);  // 13 bits, all '1's.
    return m_cacheLineCount <= MAX_CACHE_LINES_IN_SHORT_COMMAND;
}
