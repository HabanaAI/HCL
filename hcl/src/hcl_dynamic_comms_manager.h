#pragma once

#include <cstddef>               // for size_t
#include <vector>                // for vector
#include "hcl_api_types.h"       // for HCL_Comm
#include "interfaces/hcl_hal.h"  // for Hal

class HclDynamicCommunicator;
class Gen2ArchServerDef;

class HclDynamicCommsManager
{
public:
    HclDynamicCommsManager();
    virtual ~HclDynamicCommsManager();

    HclDynamicCommunicator&       getComm(const HCL_Comm commId);
    const HclDynamicCommunicator& getComm(const HCL_Comm commId) const;
    HCL_Comm                      createNextComm(hcl::HalPtr hal, Gen2ArchServerDef& serverDef);
    bool                          isCommExist(const HCL_Comm comm) const;
    size_t                        getMaxCommNum() const;

    int getNumOfActiveComms() const;

    void destroyComm(HCL_Comm comm);

private:
    std::vector<HclDynamicCommunicator*> m_communicators;
    mutable lock_t                       m_lock;
    HCL_Comm                             m_nextCommId = 1;  // 0 reserved for HCL_COMM_WORLD
    size_t                               m_size       = 0;
};