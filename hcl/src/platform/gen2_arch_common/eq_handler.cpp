#include "platform/gen2_arch_common/eq_handler.h"

#include <unistd.h>                  // for usleep
#include <exception>                 // for terminate
#include <memory>                    // for __shared_ptr_access
#include "hccl_types.h"              // for hcclInternalError
#include "hcl_config.h"              // for HclDeviceConfig
#include "hcl_exceptions.h"          // for VerifyException
#include "hcl_utils.h"               // for LOG_HCL_CRITICAL, VERIFY
#include "hlthunk.h"                 // for hlthunk_nic_eq_poll_out, hlt...
#include "interfaces/hcl_idevice.h"  // for IHclDevice
#include "drm/habanalabs_accel.h"    // for HL_NIC_EQ_POLL_STATUS_EQ_EMPTY
#include "hcl_log_manager.h"         // for LOG_*
#include "platform/gen2_arch_common/hcl_device.h"
#include "internal/dfa_defines.hpp"  // for hclNotifyFailure
#include "ibverbs/hcl_ibverbs.h"

// RXE = RX Engine (incoming)
// QPC = QP Context
// RXE = TX Engine (outgoing)
#define ERROR_CAUSE_RXE 0
#define ERROR_CAUSE_QPC 1
#define ERROR_CAUSE_TXE 2
static char errorSourceStrings[][64] = {"RXE", "QPC", "TXE"};

void IEventQueueHandler::startThread(IHclDevice* device)
{
    m_device = device;
    m_fd     = device->getFd();

    if (GCFG_HCL_USE_IBVERBS.value())
    {
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

        return;
    }

    m_thread.initialize(m_device->getDeviceConfig().getHwModuleId(),
                        m_device->getDeviceConfig().getHostName(),
                        eHCLNormalThread,
                        ([&]() {
                            try
                            {
                                while (!m_stopEqThread)
                                {
                                    handler();
                                    usleep(100000);  // 0.1 seconds
                                }
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

void IEventQueueHandler::handler()
{
    nics_mask_t enabledPortsMask = ((HclDeviceGen2Arch*)m_device)->getEnabledPortsMask();

    for (auto port : enabledPortsMask)
    {
        hlthunk_nic_eq_poll_out out;
        int                     rc = 0;

        rc = hlthunk_nic_eq_poll(m_fd, port, &out);
        VERIFY_DFA_MSG(rc == 0,
                       "eq_poll failed",
                       "Device polling failure (hlthunk_nic_eq_poll), please check if device is functional.");
        switch (out.poll_status)
        {
            case HL_NIC_EQ_POLL_STATUS_SUCCESS:
                if (parseNicEventParams(out, port))
                {
                    hclNotifyFailureV2(DfaErrorCode::hclFailed, 0, "Error in parse-nic-event-params");
                    std::terminate();
                }
                break;
            case HL_NIC_EQ_POLL_STATUS_EQ_EMPTY:
                break;
            default:
                LOG_HCL_CRITICAL(HCL, "Invalid poll_static {} from EQ poll on nic {}", out.poll_status, port);
                std::terminate();
        }
    }
}

bool IEventQueueHandler::parseNicEventParams(const hlthunk_nic_eq_poll_out& eqe, const uint32_t port)
{
    bool triggerDFA = false;

    switch (eqe.ev_type)
    {
        case HL_NIC_EQ_EVENT_TYPE_CQ_ERR:
        {
            triggerDFA = true;
            LOG_HCL_CRITICAL(HCL,
                             "Port {} device ID {} got a CQ {} error, PI 0x{:x}",
                             port,
                             m_device->getDeviceConfig().m_hwModuleID,
                             eqe.idx,
                             eqe.ev_data);
            break;
        }
        case HL_NIC_EQ_EVENT_TYPE_QP_ERR:
        {
            triggerDFA = true;
            uint8_t errorSource, errorCause, errorQpcSource;
            parseQpErrorParams(eqe.ev_data, errorSource, errorCause, errorQpcSource);
            char* sourceStr = errorSourceStrings[errorSource];
            char* causeStr;
            switch (errorSource)
            {
                case ERROR_CAUSE_RXE:
                    causeStr = getErrorCauseRXStrings(errorCause);
                    break;
                case ERROR_CAUSE_QPC:
                    causeStr = getErrorCauseQPCStrings(errorCause, errorQpcSource);
                    break;
                case ERROR_CAUSE_TXE:
                    causeStr = getErrorCauseTXStrings(errorCause);
                    break;
                default:
                    LOG_HCL_CRITICAL(HCL,
                                     "Port {} device ID {} got a QP error with invalid error cause! event data: 0x{:x}",
                                     port,
                                     m_device->getDeviceConfig().m_hwModuleID,
                                     eqe.ev_data);
                    return triggerDFA;
            }
            LOG_HCL_CRITICAL(HCL,
                             "Port {} device ID {} got an event from QP {}, Source: {}, error: {}",
                             port,
                             m_device->getDeviceConfig().m_hwModuleID,
                             eqe.idx,
                             sourceStr,
                             causeStr);
            break;
        }
        case HL_NIC_EQ_EVENT_TYPE_DB_FIFO_ERR:
        {
            triggerDFA = true;
            LOG_HCL_CRITICAL(HCL,
                             "Port {} device ID {} got a DB FIFO {} error, CI 0x{:x}",
                             port,
                             m_device->getDeviceConfig().m_hwModuleID,
                             eqe.idx,
                             eqe.ev_data);
            break;
        }
        case HL_NIC_EQ_EVENT_TYPE_CCQ:
        {
            LOG_HCL_DEBUG(HCL,
                          "Port {} device ID {} got a completion on congestion CQ {}, PI 0x{:x}",
                          port,
                          m_device->getDeviceConfig().m_hwModuleID,
                          eqe.idx,
                          eqe.ev_data);
            break;
        }
        case HL_NIC_EQ_EVENT_TYPE_WTD_SECURITY_ERR:
        {
            triggerDFA = true;
            LOG_HCL_CRITICAL(HCL,
                             "Port {} device ID {} got a WTD security error on QP {}",
                             port,
                             m_device->getDeviceConfig().m_hwModuleID,
                             eqe.idx);
            break;
        }
        case HL_NIC_EQ_EVENT_TYPE_NUMERICAL_ERR:
        {
            triggerDFA = true;
            LOG_HCL_CRITICAL(HCL,
                             "Port {} device ID {} got a numerical error on QP {}, addr 0x{:x}",
                             port,
                             m_device->getDeviceConfig().m_hwModuleID,
                             eqe.idx,
                             eqe.ev_data);
            break;
        }
        case HL_NIC_EQ_EVENT_TYPE_LINK_STATUS:
        {
            LOG_INFO_F(HCL,
                       "Port {} device ID {} got a link status change - link is now {}",
                       port,
                       m_device->getDeviceConfig().m_hwModuleID,
                       eqe.ev_data ? "UP" : "DOWN");
            break;
        }
        case HL_NIC_EQ_EVENT_TYPE_QP_ALIGN_COUNTERS:
        {
            LOG_INFO_F(HCL,
                       "Port {} device ID {} qp {} got a qp align counters event",
                       port,
                       m_device->getDeviceConfig().m_hwModuleID,
                       eqe.idx);
            break;
        }
        default:
        {
            triggerDFA = true;
            LOG_HCL_CRITICAL(HCL,
                             "Port {} device ID {} got an unknown event {}",
                             port,
                             m_device->getDeviceConfig().m_hwModuleID,
                             eqe.ev_type);
            break;
        }
    }
    return triggerDFA;
}
