#include "fault_injection_device.h"

#include <cstdint>  // for uint*
#include <mutex>    // for mutex

#include "platform/gen2_arch_common/server_connectivity.h"  // for Gen2ArchServerConnectivity
#include "hcl_utils.h"                                      // for LOG_*
#include "hccl_api_inc.h"                                   // for g_faultsStopAllApiMutex, g_faultsStopAllApi
#include "fault_tolerance_inc.h"                            // for HLFT.* macros

FaultInjectionDevice::FaultInjectionDevice(const Gen2ArchServerConnectivity& serverConnectivity,
                                           const uint32_t                    baseServerPort,
                                           NicsEventHandler&                 nicsEventsHandler)
: FaultInjectionTcpServer(serverConnectivity.getModuleId(),
                          serverConnectivity.getMaxNumScaleOutPorts(),
                          baseServerPort),
  m_serverConnectivity(serverConnectivity),
  m_nicsEventsHandler(nicsEventsHandler)
{
    HLFT_DBG("moduleId={}, numScaleoutPorts={}, m_baseServerPort={}", m_moduleId, m_numScaleoutPorts, m_baseServerPort);
}

void FaultInjectionDevice::portUp(const uint16_t portNum)
{
    HLFT_INF("m_moduleId={}, portNum={}", m_moduleId, portNum);
    // const NicState nicUpState = {0, portNum, true};
    // mcNicStateChange(nicUpState)
}

void FaultInjectionDevice::portDown(const uint16_t portNum)
{
    HLFT_INF("m_moduleId={}, portNum={}", m_moduleId, portNum);
    // const NicState nicDownState = {0, portNum, false};
    // mcNicStateChange()
}

void FaultInjectionDevice::nicUp(const uint16_t nicNum)
{
    HLFT_INF("m_moduleId={}, nicNum={}", m_moduleId, nicNum);
    m_nicsEventsHandler.nicStatusChange(nicNum, NicLkdEventsEnum::NIC_LKD_EVENTS_UP);
}

void FaultInjectionDevice::nicDown(const uint16_t nicNum)
{
    HLFT_INF("m_moduleId={}, nicNum={}", m_moduleId, nicNum);
    m_nicsEventsHandler.nicStatusChange(nicNum, NicLkdEventsEnum::NIC_LKD_EVENTS_DOWN);
}

void FaultInjectionDevice::nicShutdown(const uint16_t nicNum)
{
    HLFT_INF("m_moduleId={}, nicNum={}", m_moduleId, nicNum);
    m_nicsEventsHandler.nicStatusChange(nicNum, NicLkdEventsEnum::NIC_LKD_EVENTS_SHUTDOWN);
}

static std::mutex s_guardStopApiMutex;  // Mutex to guard the stop API activation

void FaultInjectionDevice::stopAllApi()
{
    HLFT_API_INF("Started, Current g_faultsStopAllApi={}, g_faultsCheckStopApi={}",
                 g_faultsStopAllApi.load(),
                 g_faultsCheckStopApi.load());

    // Only first thread sets the CV when atomic changes (0 -> 1)
    HLFT_DBG("Before s_guardStopApiMutex lock");
    std::lock_guard<std::mutex> lk(s_guardStopApiMutex);
    const uint32_t              commsToStopApi = g_faultsStopAllApi++;
    if (commsToStopApi == 0)
    {
        HLFT_INF("Performing Stop API");
        // Notify user API thread to block further calls
        std::lock_guard<std::mutex> lk2(g_faultsStopAllApiMutex);
        g_faultsCheckStopApi = true;  // Enable user thread to check conditions
        g_faultsStopAllApiCv.notify_all();
        HLFT_DBG("After notify");
    }
    else
    {
        HLFT_DBG("Stop flag already set");
    }
}

void FaultInjectionDevice::resumeAllApi()
{
    HLFT_INF("Started, Current g_faultsStopAllApi={}", g_faultsStopAllApi.load());

    // Check if the counter is already 0 before decrementing.
    if (g_faultsStopAllApi.load() == 0)
    {
        HLFT_ERR("Already 0 - something wrong, g_faultsCheckStopApi={}", g_faultsCheckStopApi.load());
        return;
    }

    // Last thread to decrement the counter will notify the user API thread to resume calls
    const uint32_t commsToResume = g_faultsStopAllApi--;
    if (commsToResume == 1)
    {
        std::lock_guard<std::mutex> lk(g_faultsStopAllApiMutex);
        g_faultsCheckStopApi = false;       // Disable user thread from check stop condition after it will continue
        g_faultsStopAllApiCv.notify_all();  // Notify user API thread to resume calls
        HLFT_INF("After notify, User thread unlocked");
    }
    else
    {
        HLFT_INF("Decrement still not zero");
    }
}
