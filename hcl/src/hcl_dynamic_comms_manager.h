#pragma once

#include <cstddef>               // for size_t
#include <vector>                // for vector
#include "hcl_api_types.h"       // for HCL_Comm
#include "interfaces/hcl_hal.h"  // for Hal

class HclDynamicCommunicator;

class HclDynamicCommsManager
{
public:
    HclDynamicCommsManager();
    virtual ~HclDynamicCommsManager();

    HclDynamicCommunicator& getComm(HCL_Comm commId);
    HCL_Comm                createNextComm(hcl::HalPtr hal);
    bool                    isCommExist(HCL_Comm comm);
    bool                    createHclCommWorld(hcl::HalPtr hal);

    int getNumOfActiveComms() const;

    void destroyComm(HCL_Comm comm);

private:
    std::vector<HclDynamicCommunicator*> m_communicators;
    HCL_Comm                             m_nextCommId = 1;  // 0 reserved for HCL_COMM_WORLD
    size_t                               m_size       = 0;
};