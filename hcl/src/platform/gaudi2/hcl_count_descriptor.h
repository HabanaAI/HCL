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

    bool    isShort() const;
    uint8_t numberOfActivatedNics();

private:
    uint32_t m_cellCount;
};

inline CountDescriptor::CountDescriptor(uint64_t cellCount, unsigned numNicsInUse) : m_cellCount(cellCount)
{
    UNUSED(m_cellCount);
    uint64_t tmp         = div_round_up(cellCount, NIC_CACHE_LINE_SIZE_IN_ELEMENTS);
    m_cacheLineCount     = div_round_up(tmp, numNicsInUse);
    m_cacheLineRemainder = (m_cacheLineCount * numNicsInUse) == tmp ? 0 : (m_cacheLineCount * numNicsInUse) - tmp;
    m_elementRemainder =
        (tmp * NIC_CACHE_LINE_SIZE_IN_ELEMENTS) == cellCount ? 0 : (tmp * NIC_CACHE_LINE_SIZE_IN_ELEMENTS) - cellCount;
}

inline uint8_t CountDescriptor::numberOfActivatedNics()
{
    if (m_cacheLineCount <= 3)  // only one nic works
    {
        return 6;  // 0b110
    }
    else if (m_cacheLineCount * 64 - m_cacheLineRemainder * 64 - m_elementRemainder > 0)  // all nics works
    {
        return 0;  // 0b000
    }
    else
    {
        return 4;  // only 2 nics works, 0b100
    }
}

inline bool CountDescriptor::isShort() const
{
    static constexpr uint64_t MAX_CACHE_LINES_IN_SHORT_COMMAND = ((1 << 14) - 1);  // 14 bits, all '1's
    return m_cacheLineCount <= MAX_CACHE_LINES_IN_SHORT_COMMAND;
}
