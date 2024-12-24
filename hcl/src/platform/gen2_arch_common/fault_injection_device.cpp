#include "fault_injection_device.h"

#include <cstdint>  // for uint*
#include <mutex>    // for mutx, unique_lock
#include <atomic>
#include <condition_variable>

#include "platform/gen2_arch_common/server_connectivity.h"  // for Gen2ArchServerConnectivity
#include "hcl_utils.h"                                      // for LOG_*
#include "hccl_api_inc.h"                                   // for g_faultsStopAllApiMutex, g_faultsStopAllApi

FaultInjectionDevice::FaultInjectionDevice(const Gen2ArchServerConnectivity& serverConnectivity,
                                           const uint32_t                    baseServerPort,
                                           NicsEventHandler&                 nicsEventsHandler)
: FaultInjectionTcpServer(serverConnectivity.getModuleId(),
                          serverConnectivity.getMaxNumScaleOutPorts(),
                          baseServerPort),
  m_serverConnectivity(serverConnectivity),
  m_nicsEventsHandler(nicsEventsHandler)
{
    LOG_HCL_DEBUG(HCL_FAILOVER,
                  "moduleId={}, numScaleoutPorts={}, m_baseServerPort={}",
                  m_moduleId,
                  m_numScaleoutPorts,
                  m_baseServerPort);
}

void FaultInjectionDevice::portUp(const uint16_t portNum)
{
    LOG_HCL_INFO(HCL_FAILOVER, "m_moduleId={}, portNum={}", m_moduleId, portNum);
    // const NicState nicUpState = {0, portNum, true};
    // mcNicStateChange(nicUpState)
}

void FaultInjectionDevice::portDown(const uint16_t portNum)
{
    LOG_HCL_INFO(HCL_FAILOVER, "m_moduleId={}, portNum={}", m_moduleId, portNum);
    // const NicState nicDownState = {0, portNum, false};
    // mcNicStateChange()
}

void FaultInjectionDevice::nicUp(const uint16_t nicNum)
{
    LOG_HCL_INFO(HCL_FAILOVER, "m_moduleId={}, nicNum={}", m_moduleId, nicNum);
    m_nicsEventsHandler.nicStatusChange(nicNum, NicLkdEventsEnum::NIC_LKD_EVENTS_UP);
}

void FaultInjectionDevice::nicDown(const uint16_t nicNum)
{
    LOG_HCL_INFO(HCL_FAILOVER, "m_moduleId={}, nicNum={}", m_moduleId, nicNum);
    m_nicsEventsHandler.nicStatusChange(nicNum, NicLkdEventsEnum::NIC_LKD_EVENTS_DOWN);
}

void FaultInjectionDevice::nicShutdown(const uint16_t nicNum)
{
    LOG_HCL_INFO(HCL_FAILOVER, "m_moduleId={}, nicNum={}", m_moduleId, nicNum);
    m_nicsEventsHandler.nicStatusChange(nicNum, NicLkdEventsEnum::NIC_LKD_EVENTS_SHUTDOWN);
}

void FaultInjectionDevice::stopAllApi()
{
    LOG_HCL_INFO(HCL_FAILOVER, "Current g_faultsStopAllApi={}", g_faultsStopAllApi);
    if (!g_faultsStopAllApi.exchange(true))
    {
        // Only first thread sets this flag
        LOG_HCL_DEBUG(HCL_FAILOVER, "Got hold of g_faultsStopAllApi flag");
        // Notify user API thread to block further calls
        std::lock_guard<std::mutex> lk(g_faultsStopAllApiMutex);
        g_faultsCheckStopApi = true;  // Enable user thread to check conditions
        g_faultsStopAllApiCv.notify_all();
        LOG_HCL_DEBUG(HCL_FAILOVER, "After notify");
    }
    else
    {
        LOG_HCL_DEBUG(HCL_FAILOVER, "Stop flag already set");
    }
}

void FaultInjectionDevice::resumeAllApi()
{
    LOG_HCL_INFO(HCL_FAILOVER, "Current g_faultsStopAllApi={}", g_faultsStopAllApi);
    std::lock_guard<std::mutex> lk(g_faultsStopAllApiMutex);
    LOG_HCL_DEBUG(HCL_FAILOVER, "Releasing All API");
    g_faultsStopAllApi   = false;
    g_faultsCheckStopApi = false;       // Disable user thread from check stop condition after it will continue
    g_faultsStopAllApiCv.notify_all();  // Notify user API thread to resume calls
    LOG_HCL_DEBUG(HCL_FAILOVER, "After notify");
}
