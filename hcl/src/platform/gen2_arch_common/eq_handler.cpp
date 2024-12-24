#include "platform/gen2_arch_common/eq_handler.h"

#include <unistd.h>   // for usleep
#include <exception>  // for terminate
#include <memory>     // for __shared_ptr_access

#include "hccl_types.h"              // for hcclInternalError
#include "hcl_exceptions.h"          // for VerifyException
#include "hcl_utils.h"               // for LOG_HCL_CRITICAL, VERIFY
#include "hlthunk.h"                 // for hlthunk_nic_eq_poll_out, hlt...
#include "interfaces/hcl_idevice.h"  // for IHclDevice
#include "drm/habanalabs_accel.h"    // for HL_NIC_EQ_POLL_STATUS_EQ_EMPTY
#include "hcl_log_manager.h"         // for LOG_*
#include "platform/gen2_arch_common/hcl_device.h"
#include "internal/dfa_defines.hpp"  // for hclNotifyFailure
#include "ibverbs/hcl_ibverbs.h"
#include "platform/gen2_arch_common/nics_events_hanlder_callback.h"  // for INicsEventsHandlerCallBack

IEventQueueHandler::IEventQueueHandler(INicsEventsHandlerCallBack& nicsEventsHandlerCallback)
: m_nicsEventsHandlerCallback(nicsEventsHandlerCallback)
{
}

void IEventQueueHandler::startThread(IHclDevice* device)
{
    m_device = device;
    m_fd     = device->getFd();

    m_thread.initialize(m_device->getDeviceConfig().getHwModuleId(),
                        m_device->getDeviceConfig().getHostName(),
                        eHCLNormalThread,
                        ([&]() {
                            try
                            {
                                g_ibv.eq_poll(m_stopEqThread, 100000);
                            }
                            catch (hcl::VerifyException& e)
                            {
                                g_status = hcclInternalError;
                            }
                        }));
}

void IEventQueueHandler::stopThread()
{
    m_stopEqThread = true;
    m_thread.join();
}
