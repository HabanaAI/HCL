#pragma once

#include <map>
#include <cstdint>  // for uint64_t

enum class DataOperationFlow
{
    READ_AFTER_READ = 0,
    WRITE_AFTER_WRITE,
    WRITE_AFTER_READ,
    READ_AFTER_WRITE
};

struct DeviceBufferRange
{
    uint64_t                                        m_endAddress  = 0;
    uint64_t                                        m_targetValue = 0;
    std::map<uint64_t, DeviceBufferRange>::iterator m_next;
    std::map<uint64_t, DeviceBufferRange>::iterator m_prev;

    DeviceBufferRange(uint64_t endAddress, uint64_t targetValue)
    {
        m_endAddress  = endAddress;
        m_targetValue = targetValue;
    }
};

/*
    This class hold unique device buffer ranges that are in use by the user.
    Device ranges are kept in an ordered map and a sorted linked list (by target value) of ranges.
*/
class DeviceBufferRangeManager
{
public:
    DeviceBufferRangeManager();
    virtual ~DeviceBufferRangeManager()                              = default;
    DeviceBufferRangeManager(DeviceBufferRangeManager&)              = delete;
    DeviceBufferRangeManager(DeviceBufferRangeManager&&)             = delete;
    DeviceBufferRangeManager&  operator=(DeviceBufferRangeManager&)  = delete;
    DeviceBufferRangeManager&& operator=(DeviceBufferRangeManager&&) = delete;

    std::map<uint64_t, DeviceBufferRange>           m_map;
    std::map<uint64_t, DeviceBufferRange>::iterator m_head;
    std::map<uint64_t, DeviceBufferRange>::iterator m_tail;

    void listPop();
    void listPush(std::map<uint64_t, DeviceBufferRange>::iterator it);
    void listExtract(std::map<uint64_t, DeviceBufferRange>::iterator it);
    void updateDb(uint64_t targetValue);

private:
};  // class DeviceBufferRangeManager

class DependencyChecker
{
public:
    DependencyChecker(unsigned cgSize);
    ~DependencyChecker()                               = default;
    DependencyChecker(DependencyChecker&)              = delete;
    DependencyChecker(DependencyChecker&&)             = delete;
    DependencyChecker&  operator=(DependencyChecker&)  = delete;
    DependencyChecker&& operator=(DependencyChecker&&) = delete;

    uint64_t getTargetValueForWriteRange(uint64_t address,
                                         uint64_t size,
                                         uint64_t targetValue,
                                         bool     dbModificationIsAllowed = true);
    uint64_t getTargetValueForReadRange(uint64_t address,
                                        uint64_t size,
                                        uint64_t targetValue,
                                        bool     dbModificationIsAllowed = true);
    void     updateDb(uint64_t targetValue);

private:
    DeviceBufferRangeManager m_readDb;
    DeviceBufferRangeManager m_writeDb;
    uint64_t                 m_lastTargetValue = 0;
    const unsigned           m_cgSize;

    uint64_t checkDependency(DataOperationFlow         operationFlow,
                             DeviceBufferRangeManager& db,
                             uint64_t                  address,
                             uint64_t                  size,
                             uint64_t                  targetValue,
                             bool                      dbModificationIsAllowed = true);

};  // class DependencyChecker