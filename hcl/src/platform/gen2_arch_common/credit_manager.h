#pragma once

#include <cstdint>  // for int64_t, uint64_t, uint32_t
#include <vector>   // for vector

class CreditManager
{
public:
    CreditManager() = default;
    CreditManager(unsigned poolSize);
    virtual ~CreditManager() = default;

    uint64_t allocNextCredit(uint64_t targetValue);
    uint64_t getCurrentCredit();
    int64_t  getCurrentTargetValue();

    void            advanceProg(uint64_t currTargetValue);
    bool            isCreditExpiring();
    inline unsigned getPoolSize() { return m_poolSize; }

protected:
    int getCurrentCreditIndex(bool inc);

    unsigned m_poolSize;
    unsigned m_currentCreditIdx;

    std::vector<uint64_t> m_creditExpirations;

    // assuming no overlaps
    uint64_t m_creditExpirationsAtTargetValue = 0;

    uint64_t m_currTargetValue;
};
