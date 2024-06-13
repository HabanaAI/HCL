#include "dependency_checker.h"

#include "hcl_utils.h"  // for VERIFY

DeviceBufferRangeManager::DeviceBufferRangeManager() : m_head(m_map.end()), m_tail(m_map.end()) {}

void DeviceBufferRangeManager::listPop()
{
    m_tail = m_tail->second.m_next;
    if (m_tail != m_map.end())
    {
        m_tail->second.m_prev = m_map.end();
    }
    else
    {
        m_head = m_map.end();
    }
}

void DeviceBufferRangeManager::listPush(std::map<uint64_t, DeviceBufferRange>::iterator it)
{
    if (m_head != m_map.end())
    {
        // the list is not empty
        it->second.m_prev     = m_head;
        m_head->second.m_next = it;
    }
    else
    {
        // the list is empty
        m_tail            = it;
        it->second.m_prev = m_map.end();
    }

    m_head            = it;
    it->second.m_next = m_map.end();
}

void DeviceBufferRangeManager::listExtract(std::map<uint64_t, DeviceBufferRange>::iterator it)
{
    if (it->second.m_next != m_map.end())
    {
        it->second.m_next->second.m_prev = it->second.m_prev;
    }

    if (it->second.m_prev != m_map.end())
    {
        it->second.m_prev->second.m_next = it->second.m_next;
    }

    if (m_tail == it)
    {
        m_tail = it->second.m_next;
    }

    if (m_head == it)
    {
        m_head = it->second.m_prev;
    }
}

void DeviceBufferRangeManager::updateDb(uint64_t targetValue)
{
    while ((m_tail != m_map.end()) && (m_tail->second.m_targetValue <= targetValue))
    {
        auto it = m_tail;
        listPop();
        m_map.erase(it);
    }
}

DependencyChecker::DependencyChecker(unsigned cgSize) : m_cgSize(cgSize) {}

bool doRangesIntersect(uint64_t startAddrA, uint64_t endAddrA, uint64_t startAddrB, uint64_t endAddrB)
{
    return (startAddrA < endAddrB) && (endAddrA > startAddrB);
}

/*
   Check if the given device buffer range overlaps with previous ranges and if so return target value that this
   collective should wait until its done (using credits mechanism). In READ_AFTER_READ if there is an overlap, we should
   allow them to run simultaneously, so we will create a merged range, remove the old entries and add a new merged range
   with updated target value. In WRITE_AFTER_WRITE if there is an overlap, we don't allow them to run simultaneously, so
   we will create a merged range, remove the old entries and add a new merged range with updated target value and return
   the highest target value of the overlapped ranges. In WRITE_AFTER_READ or READ_AFTER_WRITE if there is an overlap, we
   don't update the data base, only return the highest target value of the overlapped ranges.
*/
uint64_t DependencyChecker::checkDependency(DataOperationFlow         operationFlow,
                                            DeviceBufferRangeManager& db,
                                            uint64_t                  address,
                                            uint64_t                  size,
                                            uint64_t                  targetValue,
                                            bool                      dbModificationIsAllowed)
{
    uint64_t rcTargetValue = 0;
    // when we only check for dependencies without updating the db, we should be as strict as possible.
    if (!dbModificationIsAllowed) operationFlow = DataOperationFlow::READ_AFTER_WRITE;

    if (db.m_map.empty())
    {
        if (dbModificationIsAllowed && (operationFlow == DataOperationFlow::READ_AFTER_READ ||
                                        operationFlow == DataOperationFlow::WRITE_AFTER_WRITE))
        {
            const auto& pair = db.m_map.emplace(address, DeviceBufferRange(address + size, targetValue));
            db.listPush(pair.first);
        }

        return rcTargetValue;
    }

    std::map<uint64_t, DeviceBufferRange>::iterator itFirst = db.m_map.end();
    std::map<uint64_t, DeviceBufferRange>::iterator itLast  = db.m_map.end();

    uint64_t addressEnd = address + size;
    // First element greater than the given address
    std::map<uint64_t, DeviceBufferRange>::iterator itRange = db.m_map.upper_bound(address);
    if (itRange != db.m_map.end())
    {
        if (addressEnd > itRange->first)
        {
            itFirst = itRange;
        }

        if (itRange != db.m_map.begin())
        {
            itRange--;
            if (itRange->second.m_endAddress > address)
            {
                itFirst = itRange;
            }
        }
    }
    else
    {
        // Check last range in map.
        itRange--;
        if (doRangesIntersect(address, addressEnd, itRange->first, itRange->second.m_endAddress))
        {
            itFirst = itRange;
        }
    }

    if (itFirst != db.m_map.end())
    {
        // Found the first range that intersect, now lets find the last range.
        uint64_t addressEnd = address + size;
        itRange             = db.m_map.lower_bound(addressEnd);
        if (itRange != db.m_map.end())
        {
            if (itRange != db.m_map.begin())
            {
                itRange--;
                if (doRangesIntersect(address, addressEnd, itRange->first, itRange->second.m_endAddress))
                {
                    itLast = itRange;
                }
            }
        }
        else
        {
            itRange = db.m_map.end();
            itRange--;
            if (doRangesIntersect(address, addressEnd, itRange->first, itRange->second.m_endAddress))
            {
                itLast = itRange;
            }
        }

        VERIFY(itLast != db.m_map.end(), "Expected itLast to be valid");

        if (operationFlow == DataOperationFlow::READ_AFTER_READ)
        {
            // In Read after Read - we merge ranges and give them an updated targetValue.
            address    = std::min(address, itFirst->first);
            addressEnd = std::max(addressEnd, itLast->second.m_endAddress);
        }
        else if (operationFlow == DataOperationFlow::WRITE_AFTER_WRITE)
        {
            for (std::map<uint64_t, DeviceBufferRange>::iterator it = itFirst; it != std::next(itLast); it++)
            {
                // Since in group context we only update the db and don't signal dependency to the user we have to merge
                // ranges, to keep the db correctness for future operations. In case we will support dependency checker
                // inside group context, we should merge only ranges with the same target value as the this new range.
                address    = std::min(address, it->first);
                addressEnd = std::max(addressEnd, it->second.m_endAddress);
                if (it->second.m_targetValue != targetValue)
                {
                    rcTargetValue = std::max(rcTargetValue, it->second.m_targetValue);
                }
            }
        }
        else  // READ_AFTER_WRITE or WRITE_AFTER_READ
        {
            for (std::map<uint64_t, DeviceBufferRange>::iterator it = itFirst; it != std::next(itLast); it++)
            {
                if (it->second.m_targetValue != targetValue)
                {
                    rcTargetValue = std::max(rcTargetValue, it->second.m_targetValue);
                }
            }
        }
    }

    if (dbModificationIsAllowed &&
        (operationFlow == DataOperationFlow::READ_AFTER_READ || operationFlow == DataOperationFlow::WRITE_AFTER_WRITE))
    {
        // No dependency - add entry
        if (itFirst == db.m_map.end())
        {
            auto pair = db.m_map.emplace(address, DeviceBufferRange(addressEnd, targetValue));
            db.listPush(pair.first);
        }
        else
        {
            for (std::map<uint64_t, DeviceBufferRange>::iterator it = itFirst; it != std::next(itLast); it++)
            {
                db.listExtract(it);
            }

            if (itFirst != itLast)
            {
                db.m_map.erase(itFirst, ++itLast);
            }
            else
            {
                db.m_map.erase(itFirst);
            }

            auto pair = db.m_map.emplace(address, DeviceBufferRange(addressEnd, targetValue));
            db.listPush(pair.first);
        }
    }

    return rcTargetValue;
}

void DependencyChecker::updateDb(uint64_t targetValue)
{
    // If there isn't dependency - remove "old" entries
    if (targetValue == 0)
    {
        targetValue = (m_lastTargetValue > m_cgSize) ? m_lastTargetValue - m_cgSize : 0;
    }

    m_readDb.updateDb(targetValue);
    m_writeDb.updateDb(targetValue);
}

uint64_t DependencyChecker::getTargetValueForWriteRange(uint64_t address,
                                                        uint64_t size,
                                                        uint64_t targetValue,
                                                        bool     dbModificationIsAllowed)
{
    VERIFY(m_lastTargetValue <= targetValue,
           "Unexpected targetValue={}, expected to be at least {}",
           targetValue,
           m_lastTargetValue);

    if (dbModificationIsAllowed) m_lastTargetValue = targetValue;
    uint64_t rcTargetValue = 0;

    if (size != 0)
    {
        rcTargetValue = checkDependency(DataOperationFlow::WRITE_AFTER_READ, m_readDb, address, size, targetValue, dbModificationIsAllowed);
        rcTargetValue =
            std::max(rcTargetValue,
                     checkDependency(DataOperationFlow::WRITE_AFTER_WRITE, m_writeDb, address, size, targetValue, dbModificationIsAllowed));
    }

    if (dbModificationIsAllowed) updateDb(rcTargetValue);

    return rcTargetValue;
}

uint64_t DependencyChecker::getTargetValueForReadRange(uint64_t address,
                                                       uint64_t size,
                                                       uint64_t targetValue,
                                                       bool     dbModificationIsAllowed)
{
    VERIFY(m_lastTargetValue <= targetValue,
           "Unexpected targetValue={}, expected to be at least {}",
           targetValue,
           m_lastTargetValue);
    if (dbModificationIsAllowed) m_lastTargetValue = targetValue;
    uint64_t rcTargetValue = 0;

    if (size != 0)
    {
        rcTargetValue = checkDependency(DataOperationFlow::READ_AFTER_READ, m_readDb, address, size, targetValue, dbModificationIsAllowed);
        rcTargetValue =
            std::max(rcTargetValue,
                     checkDependency(DataOperationFlow::READ_AFTER_WRITE, m_writeDb, address, size, targetValue, dbModificationIsAllowed));
    }

    if (dbModificationIsAllowed) updateDb(rcTargetValue);

    return rcTargetValue;
}
