#include <cstdint>
#include <numeric>

#include "credit_manager.h"

#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_*
#include "hcl_math_utils.h"

CreditManager::CreditManager(unsigned poolSize) : m_poolSize(poolSize), m_currentCreditIdx(-1), m_currTargetValue(-1)
{
    m_creditExpirations.resize(m_poolSize, 0);
}

int CreditManager::getCurrentCreditIndex(bool inc)
{
    if (inc)
    {
        m_currentCreditIdx++;
        m_currentCreditIdx = mod(m_currentCreditIdx, m_poolSize);
    }

    return m_currentCreditIdx;
}

uint64_t CreditManager::allocNextCredit(uint64_t targetValue)
{
    unsigned idx             = getCurrentCreditIndex(true);
    uint64_t prevTargetValue = m_creditExpirations[idx];

    VERIFY(prevTargetValue != targetValue, "No available intermediate buffer");

    m_creditExpirations[idx] = targetValue;

    VERIFY(targetValue >= m_creditExpirationsAtTargetValue,
           "unexpected expiration value, new({}), old({}) ",
           targetValue,
           m_creditExpirationsAtTargetValue);
    m_creditExpirationsAtTargetValue = targetValue;

    return prevTargetValue;
}

uint64_t CreditManager::getCurrentCredit()
{
    return getCurrentCreditIndex(false);
}

int64_t CreditManager::getCurrentTargetValue()
{
    return m_creditExpirations[getCurrentCreditIndex(false)];
}

void CreditManager::advanceProg(uint64_t currTargetValue)
{
    m_currTargetValue = currTargetValue;
}

bool CreditManager::isCreditExpiring()
{
    if (m_creditExpirationsAtTargetValue == m_currTargetValue)
    {
        return true;
    }
    return false;
}
