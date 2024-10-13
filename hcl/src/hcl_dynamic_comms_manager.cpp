#include "hcl_dynamic_comms_manager.h"

#include <algorithm>                               // for max
#include "hcl_api_types.h"                         // for HCL_Comm, HCL_COMM_WORLD
#include "hcl_dynamic_communicator.h"              // for HclDynamicCommunicator
#include "hcl_types.h"                             // for DEFAULT_COMMUNICATORS_SIZE
#include "hcl_utils.h"                             // for VERIFY
#include "hcl_log_manager.h"                       // for LOG_*
#include "interfaces/hcl_hal.h"                    // for HalPtr
#include "platform/gen2_arch_common/server_def.h"  // for Gen2ArchServerDef

HclDynamicCommsManager::HclDynamicCommsManager()
{
    m_communicators.resize(DEFAULT_COMMUNICATORS_SIZE, nullptr);
}

HclDynamicCommsManager::~HclDynamicCommsManager()
{
    if (m_size == 0) return;

    LOG_WARN(HCL,
             "There are {} communicators left during teardown, meaning not all communicators have been destroyed",
             m_communicators.size());
    for (HclDynamicCommunicator* communicator : m_communicators)
    {
        delete communicator;
    }
}

HCL_Comm HclDynamicCommsManager::createNextComm(hcl::HalPtr hal, Gen2ArchServerDef& serverDef)
{
    HCL_Comm comm = m_nextCommId++;
    if (unlikely(comm >= m_communicators.size()))
    {
        // push_back may cause a resize of the underlying continuous array, hence a new-delete sequence followed by a
        // none-free memcpy.
        LOG_HCL_DEBUG(HCL, "Resizing m_communicators for new comm({})", comm);
        m_communicators.resize(m_communicators.size() + DEFAULT_COMMUNICATORS_SIZE, nullptr);
    }

    // By default we should hope that the above sequence can be avoided - if the array is resized by default to a
    // big enough size, this will result in a simple assignment.
    m_communicators[comm] = new HclDynamicCommunicator(comm, serverDef, hal);
    m_size++;
    return comm;
}

HclDynamicCommunicator& HclDynamicCommsManager::getComm(HCL_Comm commId)
{
    VERIFY(isCommExist(commId), "comm({}) does not exist", commId);
    return *m_communicators[commId];
}

bool HclDynamicCommsManager::isCommExist(HCL_Comm comm)
{
    return comm < m_nextCommId && m_communicators[comm] != nullptr;
}

void HclDynamicCommsManager::destroyComm(HCL_Comm comm)
{
    if (isCommExist(comm))
    {
        delete m_communicators[comm];
        m_communicators[comm] = nullptr;
        m_size--;
    }
}

int HclDynamicCommsManager::getNumOfActiveComms() const
{
    return m_size;
}