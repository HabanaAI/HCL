#include "platform/gen2_arch_common/host_scheduler.h"

#include <string.h>  // for memcpy
#include <string>    // for to_string
#include <memory>    // for __shared_ptr_a...

#include "hcl_exceptions.h"                            // for VerifyException
#include "hcl_utils.h"                                 // for LOG_HCL_*
#include "ofi_communicator.h"                          // for HclDynamicCommunicator
#include "hcl_log_manager.h"                           // for LOG_*
#include "platform/gen2_arch_common/hcl_device.h"      // for HclDeviceGen2Arch
#include "hccl/hccl_internal_defs.h"                   // for hcclHandle
#include "hcl_dynamic_communicator.h"                  // for HclDynamicCommunicator
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for Gen2ArchScalManager
#include "hcl_global_conf.h"                           // for GCFG_...
#include "infra/hcl_debug_stats.h"                     // for DEBUG_STATS_...

void HostScheduler::startThread(HclDeviceGen2Arch* device, unsigned index, std::vector<HostStream*>& hostStreams)
{
    m_hostStreams    = hostStreams;
    m_stop           = false;
    m_device         = device;
    m_index          = index;
    m_sleepThreshold = GCFG_HOST_SCHEDULER_SLEEP_THRESHOLD.value();
    m_sleepDuration  = std::chrono::milliseconds(GCFG_HOST_SCHEDULER_SLEEP_DURATION.value());
    m_thread.initialize(m_device->getDeviceConfig().getHwModuleId(),
                        m_device->getDeviceConfig().getHostName(),
                        eHCLProactorThread,
                        &HostScheduler::runHostScheduler,
                        this);
}

void HostScheduler::stopThread()
{
    LOG_HCL_INFO(HCL, "Stopping host scheduler thread ({})", m_index);
    if (!m_stop)
    {
        m_stop = true;
        notifyThread();
        m_thread.join();
    }
}

void HostScheduler::notifyThread()
{
    std::unique_lock<std::mutex> lock(m_submittedWorkMutex);
    m_submittedWork = true;
    m_submittedWorkCondVar.notify_one();
}

HostScheduler::~HostScheduler()
{
    stopThread();
}

void HostScheduler::runHostScheduler()
{
    try
    {
        unsigned emptyStreamsCounter = 0;
        while (!m_stop)
        {
            bool allStreamsAreEmpty = true;
            for (const auto& hostStream : m_hostStreams)
            {
                if (!hostStream->isEmpty())
                {
                    processStream(hostStream);
                    allStreamsAreEmpty  = false;
                    emptyStreamsCounter = 0;
                }
            }

            if (allStreamsAreEmpty)
            {
                emptyStreamsCounter++;
            }

            if (emptyStreamsCounter >= m_sleepThreshold)
            {
                {
                    std::unique_lock<std::mutex> lock(m_submittedWorkMutex);
                    // Before changing m_submittedWork, and to avoid a race condition with the main thread, need to make
                    // sure no job was added.
                    for (const auto& hostStream : m_hostStreams)
                    {
                        if (!hostStream->isEmpty())
                        {
                            emptyStreamsCounter = 0;
                            break;
                        }
                    }

                    if (emptyStreamsCounter == 0)
                    {
                        continue;  // continue to while (!m_stop) loop
                    }

                    m_submittedWork = false;
                }
                {
                    std::unique_lock<std::mutex> lock(m_submittedWorkMutex);
                    // Verify that the main thread didn't submit work, before going to sleep
                    if (m_submittedWork == false)
                    {
                        m_submittedWorkCondVar.wait_for(lock, m_sleepDuration);
                    }
                }
            }
        }
    }
    catch (hcl::VerifyException& e)
    {
        g_status = hcclInternalError;
    }
}

void HostScheduler::processStream(HostStream* hostStream)
{
    uint64_t size            = 0;
    bool     done            = false;
    uint32_t streamDepthProc = getStreamDepthProc(hostStream);

    do
    {
        m_hostStreamCmd = hostStream->getOuterQueue()->read(&size);
        if (size == 0) return;

        const uint8_t op          = (*(uint8_t*)m_hostStreamCmd) & 0xF;
        uint32_t      commandSize = 0;

        PROFILER_CONTEXT_INIT(hostStream->getStreamName().c_str());

        switch (op)
        {
            case HOST_SCHED_CMD_FENCE_WAIT:
            {
                done        = processFenceWaitCommand(hostStream);
                commandSize = sizeof(host_sched_cmd_fence_wait);
                break;
            }

            case HOST_SCHED_CMD_SEND_WITH_FENCE:
            case HOST_SCHED_CMD_RECV_WITH_FENCE:
            {
                done        = processScaleOutWithFenceCommand(hostStream);
                commandSize = sizeof(host_sched_cmd_scale_out_with_fence_nic_op);
                break;
            }

            case HOST_SCHED_CMD_SEND:
            case HOST_SCHED_CMD_RECV:
            {
                done        = processScaleOutCommand(hostStream);
                commandSize = sizeof(host_sched_cmd_scale_out_nic_op);
                break;
            }

            case HOST_SCHED_CMD_WAIT_FOR_COMP:
            {
                uint64_t srCount            = 0;
                uint64_t submitTime         = 0;
                done                        = processScaleoutWaitForCompCommand(hostStream, srCount, submitTime);
                commandSize                 = sizeof(host_sched_cmd_wait_for_completion);
                const uint64_t currTime     = hostStream->getCurrTimeMsec();
                const uint64_t durationMsec = currTime - submitTime;

                if (done && unlikely(LOG_LEVEL_AT_LEAST_WARN(HCL_OFI)))
                {
                    VERIFY((submitTime > 0) && (srCount > 0),
                           "submitTime & srCount cannot be 0, stream={}, srCount={}, submitTime={}",
                           hostStream->getStreamName(),
                           srCount,
                           submitTime);
                    const uint64_t timerThresholdMsec = GCFG_HOST_SCHEDULER_OFI_DELAY_MSG_THRESHOLD.value();
                    if (unlikely(durationMsec >= timerThresholdMsec))
                    {
                        LOG_HCL_WARN(HCL_OFI,
                                     "Done, stream {}, srCount={}, durationMsec={} msec is over threshold of {} msec",
                                     hostStream->getStreamName(),
                                     srCount,
                                     durationMsec,
                                     timerThresholdMsec);
                    }
                    break;
                }
                if (!done && submitTime != 0)
                {
                    // This code will log a critical error if we are waiting on the SO send ACK for more then threshold
                    // milliseconds
                    if (unlikely(durationMsec >= GCFG_HOST_SCHEDULER_OFI_DELAY_ACK_THRESHOLD.value()))
                    {
                        // We need to save the the last log time to prevent log flooding
                        static uint64_t lastLogTime      = submitTime;
                        const uint64_t  timeSinceLastLog = currTime - lastLogTime;

                        // Print to log only if the time since the last log exceeded the threshold
                        if (timeSinceLastLog > GCFG_HOST_SCHEDULER_OFI_DELAY_ACK_THRESHOLD_LOG_INTERVAL.value() ||
                            lastLogTime == submitTime)
                        {
                            LOG_HCL_CRITICAL(HCL_OFI,
                                             "Stream {} transaction #{} is stuck for {} milliseconds",
                                             hostStream->getStreamName(),
                                             srCount,
                                             durationMsec);
                            lastLogTime = currTime;
                        }
                    }
                }

                break;
            }

            case HOST_SCHED_CMD_SIGNAL_SO:
            {
                done        = processSignalSoCommand(hostStream);
                commandSize = sizeof(host_sched_cmd_signal_so);
                break;
            }

            default:
                VERIFY(false, "On stream {}, unknown host stream command opcode ({})", hostStream->getStreamName(), op);
        }

        if (done)
        {
            hostStream->getOuterQueue()->free(commandSize >> 2);

            if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= DEBUG_STATS_LOW) && hostStream->getOnGoingProcessing())
            {
                std::string srCount = std::to_string(hostStream->getCurrentSrCountProcessing());
                const char* args[]  = {"srCount", srCount.c_str()};
                HCL_FUNC_INSTRUMENTATION_STRING_ARGS_COMPLETE(DEBUG_STATS_LOW,
                                                              hostStream->getOnGoingFuncName(),
                                                              "",
                                                              args,
                                                              2);
                hostStream->setOnGoingProcessing(false);
            }

            streamDepthProc--;
        }
        else
        {
            streamDepthProc = 0;
        }
    } while (streamDepthProc);
}

bool HostScheduler::processScaleoutWaitForCompCommand(HostStream* hostStream, uint64_t& srCount, uint64_t& submitTime)
{
    host_sched_cmd_wait_for_completion* waitForCompCommand = (host_sched_cmd_wait_for_completion*)m_hostStreamCmd;

    if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= DEBUG_STATS_LOW) && !hostStream->getOnGoingProcessing())
    {
        hostStream->setOnGoingProcessing(true);
        hostStream->setCurrentSrCountProcessing(waitForCompCommand->srCount);
        hostStream->setOnGoingFuncName(m_cmdNames.getCommandName(waitForCompCommand->opcode) + " - " +
                                       hostStream->getStreamName());
        HCL_FUNC_INSTRUMENTATION_STRING_START(DEBUG_STATS_LOW, hostStream->getOnGoingFuncName());
    }

    uint64_t size = 0;
    int      done = 0;

    innerQueueMsg* internalStreamInfo = (innerQueueMsg*)hostStream->getInnerQueue()->read(&size);
    if (size == 0)
    {
        return false;
    }

    bool status = m_device->getComm(waitForCompCommand->comm)
                      .m_hostNicBridge->waitForCompletionNb(&internalStreamInfo->handle, done);
    VERIFY(status == true, "waitForCompletion returned with an error");

    if (done)
    {
        hostStream->getInnerQueue()->free(sizeof(innerQueueMsg) >> 2);
        srCount = internalStreamInfo->srCount;
    }
    submitTime = internalStreamInfo->submitTime;

    return done;
}

bool HostScheduler::processScaleOutWithFenceCommand(HostStream* hostStream)
{
    host_sched_cmd_scale_out_with_fence_nic_op* scaleOutCommand =
        (host_sched_cmd_scale_out_with_fence_nic_op*)m_hostStreamCmd;

    if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= DEBUG_STATS_LOW) && !hostStream->getOnGoingProcessing())
    {
        hostStream->setOnGoingProcessing(true);
        hostStream->setCurrentSrCountProcessing(scaleOutCommand->srCount);
        hostStream->setOnGoingFuncName(m_cmdNames.getCommandName(scaleOutCommand->opcode) + " - " +
                                       hostStream->getStreamName());
        HCL_FUNC_INSTRUMENTATION_STRING_START(DEBUG_STATS_LOW, hostStream->getOnGoingFuncName());
    }

    bool waitOnFence = m_device->getScalManager().hostWaitOnFence(hostStream->getArchStreamIdx(),
                                                                  scaleOutCommand->fenceIdx,
                                                                  scaleOutCommand->askForCredit);

    // The first time we trigger the fence, 'askForCredit' should be 1. This tells the fence's SOB to decrement by one
    // (ask for a credit). The next time we trigger the fence, the credit has already been asked so we don't need to
    // ask again.
    scaleOutCommand->askForCredit = 0;

    if (waitOnFence)
    {
        return false;
    }

    bool isSend = scaleOutCommand->opcode == HOST_SCHED_CMD_SEND_WITH_FENCE;

    uint64_t address = scaleOutCommand->address;
    int      rank    = scaleOutCommand->rank;
    uint64_t size    = scaleOutCommand->size;
    HCL_Comm comm    = scaleOutCommand->comm;

    hcclHandle   handle;
    hcclResult_t status;

    if (isSend)
    {
        status = m_device->getComm(comm).m_hostNicBridge->sendAsync((void*)address,
                                                                    size,
                                                                    rank,
                                                                    &handle,
                                                                    hostStream->getUarchStreamIdx(),
                                                                    scaleOutCommand->compParams,
                                                                    scaleOutCommand->qpSetIndex);
    }
    else
    {
        status = m_device->getComm(comm).m_hostNicBridge->recvAsync((void*)address,
                                                                    size,
                                                                    rank,
                                                                    &handle,
                                                                    hostStream->getUarchStreamIdx(),
                                                                    scaleOutCommand->compParams,
                                                                    scaleOutCommand->qpSetIndex);
    }

    if (status != hcclSuccess)
    {
        LOG_HCL_ERR(HCL, "[{}]: {} returned with an error", m_index, isSend ? "sendAsync" : "recvAsync");
    }

    innerQueueMsg innerMsg;
    innerMsg.handle     = handle.ofi;
    innerMsg.submitTime = hostStream->getCurrTimeMsec();
    innerMsg.srCount    = scaleOutCommand->srCount;

    uint32_t* ptr = hostStream->getInnerQueue()->getNextPtr(sizeof(innerQueueMsg) >> 2);
    memcpy((void*)ptr, &innerMsg, sizeof(innerQueueMsg));

    hostStream->getInnerQueue()->submit();

    return true;
}

bool HostScheduler::processScaleOutCommand(HostStream* hostStream)
{
    host_sched_cmd_scale_out_nic_op* scaleOutCommand = (host_sched_cmd_scale_out_nic_op*)m_hostStreamCmd;

    if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= DEBUG_STATS_LOW) && !hostStream->getOnGoingProcessing())
    {
        hostStream->setOnGoingProcessing(true);
        hostStream->setCurrentSrCountProcessing(scaleOutCommand->srCount);
        hostStream->setOnGoingFuncName(m_cmdNames.getCommandName(scaleOutCommand->opcode) + " - " +
                                       hostStream->getStreamName());
        HCL_FUNC_INSTRUMENTATION_STRING_START(DEBUG_STATS_LOW, hostStream->getOnGoingFuncName());
    }

    bool isSend = scaleOutCommand->opcode == HOST_SCHED_CMD_SEND;

    uint64_t address = scaleOutCommand->address;
    int      rank    = scaleOutCommand->rank;
    uint64_t size    = scaleOutCommand->size;
    HCL_Comm comm    = scaleOutCommand->comm;

    hcclHandle   handle;
    hcclResult_t status;

    if (isSend)
    {
        status = m_device->getComm(comm).m_hostNicBridge->sendAsync((void*)address,
                                                                    size,
                                                                    rank,
                                                                    &handle,
                                                                    hostStream->getUarchStreamIdx(),
                                                                    scaleOutCommand->compParams,
                                                                    scaleOutCommand->qpSetIndex);
    }
    else
    {
        status = m_device->getComm(comm).m_hostNicBridge->recvAsync((void*)address,
                                                                    size,
                                                                    rank,
                                                                    &handle,
                                                                    hostStream->getUarchStreamIdx(),
                                                                    scaleOutCommand->compParams,
                                                                    scaleOutCommand->qpSetIndex);
    }

    if (status != hcclSuccess)
    {
        LOG_HCL_ERR(HCL, "[{}]: {} returned with an error", m_index, isSend ? "sendAsync" : "recvAsync");
    }

    innerQueueMsg innerMsg;
    innerMsg.handle     = handle.ofi;
    innerMsg.submitTime = hostStream->getCurrTimeMsec();
    innerMsg.srCount    = scaleOutCommand->srCount;

    uint32_t* ptr = hostStream->getInnerQueue()->getNextPtr(sizeof(innerQueueMsg) >> 2);
    memcpy((void*)ptr, &innerMsg, sizeof(innerQueueMsg));

    hostStream->getInnerQueue()->submit();

    return true;
}

bool HostScheduler::processSignalSoCommand(HostStream* hostStream)
{
    host_sched_cmd_signal_so* signalSoCommand = (host_sched_cmd_signal_so*)m_hostStreamCmd;

    m_device->getScalManager().signalFromHost(signalSoCommand->compParams.smIdx,
                                              signalSoCommand->compParams.soIdx,
                                              signalSoCommand->compParams.value);

    return true;
}

bool HostScheduler::processFenceWaitCommand(HostStream* hostStream)
{
    host_sched_cmd_fence_wait* fenceWaitCommand = (host_sched_cmd_fence_wait*)m_hostStreamCmd;

    if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= DEBUG_STATS_LOW) && !hostStream->getOnGoingProcessing())
    {
        hostStream->setOnGoingProcessing(true);
        hostStream->setCurrentSrCountProcessing(fenceWaitCommand->srCount);
        hostStream->setOnGoingFuncName(m_cmdNames.getCommandName(fenceWaitCommand->opcode) + " - " +
                                       hostStream->getStreamName());
        HCL_FUNC_INSTRUMENTATION_STRING_START(DEBUG_STATS_LOW, hostStream->getOnGoingFuncName());
    }

    bool waitOnFence = m_device->getScalManager().hostWaitOnFence(hostStream->getArchStreamIdx(),
                                                                  fenceWaitCommand->fenceIdx,
                                                                  fenceWaitCommand->askForCredit);

    // The first time we trigger the fence, 'askForCredit' should be 1. This tells the fence's SOB to decrement by one
    // (ask for a credit). The next time we trigger the fence, the credit has already been asked so we don't need to
    // ask again.
    fenceWaitCommand->askForCredit = 0;

    return !waitOnFence;
}

uint32_t HostScheduler::getStreamDepthProc(HostStream* hostStream)
{
    const HostStreamType type = hostStream->getType();
    return (type == HOST_STREAM_SEND || type == HOST_STREAM_RECV) ? GCFG_HOST_SCHEDULER_STREAM_DEPTH_PROC.value() : 1;
}
