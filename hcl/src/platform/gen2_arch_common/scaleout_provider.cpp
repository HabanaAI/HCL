#include "platform/gen2_arch_common/scaleout_provider.h"
#include <algorithm>
#include <array>
#include <string>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include "hccl/ofi_communicator.h"
#include "hcl_dynamic_communicator.h"
#include "hcl_global_conf.h"
#include "interfaces/hcl_remote_device.h"
#include "hcl_utils.h"
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for
#include "interfaces/hcl_hal.h"
#include "interfaces/hcl_unique_sorted_vector.h"
#include "platform/gen2_arch_common/collective_states.h"
#include "platform/gen2_arch_common/hcl_device.h"
#include "platform/gen2_arch_common/host_scheduler.h"
#include "platform/gen2_arch_common/host_stream.h"
#include "platform/gen2_arch_common/intermediate_buffer_container.h"
#include "platform/gen2_arch_common/host_buffer_manager.h"
#include "platform/gen2_arch_common/signals/manager.h"
#include "platform/gen2_arch_common/signals/types.h"
#include "platform/gen2_arch_common/collective_utils.h"  // for getNextBox, getPrevBox
#include "hcl_log_manager.h"                             // for LOG_*
#include "hcl_types.h"                                   // for HostNicConnectInfo
#include "hcl_math_utils.h"
#include "platform/gen2_arch_common/server_connectivity.h"  // for Gen2ArchServerConnectivity

ScaleoutProvider::ScaleoutProvider(HclDeviceGen2Arch* device) : m_device(device) {}

ScaleoutProvider* ScaleoutProvider::createScaleOutProvider(HclDeviceGen2Arch* device)
{
    ScaleoutProvider* provider = nullptr;
    if (GCFG_HCCL_OVER_OFI.value())
    {
        provider = new LibfabricScaleoutProvider(device);
    }
    else
    {
        provider = new Gen2ArchScaleoutProvider(device);
    }
    return provider;
}

HostBufferManager* ScaleoutProvider::getHostBufferManager([[maybe_unused]] unsigned streamIdx)
{
    VERIFY(false, "This scaleout provider does not support host-mapped buffers");
    return nullptr;
}

Gen2ArchScaleoutProvider::Gen2ArchScaleoutProvider(HclDeviceGen2Arch* device) : ScaleoutProvider(device)
{
    LOG_HCL_INFO(HCL, "Scale-Out provider - gaudi");
}

bool Gen2ArchScaleoutProvider::isHostNic() const
{
    return false;
}

bool Gen2ArchScaleoutProvider::isGaudiDirect() const
{
    return false;
}

SignalEvent Gen2ArchScaleoutProvider::getScaleoutSendSignal()
{
    return SignalEvent::SCALEOUT_SEND;
}

SignalEvent Gen2ArchScaleoutProvider::getScaleoutRecvSignal([[maybe_unused]] bool doReduction)
{
    return SignalEvent::SCALEOUT_RECV;
}

int Gen2ArchScaleoutProvider::getInternalScaleoutFences()
{
    return 0;
}

void Gen2ArchScaleoutProvider::validateSize([[maybe_unused]] uint64_t size)
{
    return;
}

int Gen2ArchScaleoutProvider::setInternalScaleoutRecvWait([[maybe_unused]] WaitMethod      method,
                                                          [[maybe_unused]] SignalsManager& signalsManager,
                                                          [[maybe_unused]] bool            doReduction)
{
    return 0;  // no internal waits
}

void Gen2ArchScaleoutProvider::requestScaleoutResources(SliceState& sliceState, SignalsManager& signalsManager)
{
    if (!sliceState.isScaleoutRequired(sliceState.m_isSend, sliceState.m_boxNumInfo))
    {
        LOG_HCL_TRACE(HCL, "Scale out is NOT required");
        return;
    }
    LOG_HCL_TRACE(HCL,
                  "requesting resources for currentOp {} (will signal to {} using event {})",
                  sliceState.m_currentOp,
                  sliceState.m_execution.m_scaleoutCompletionWaitMethod,
                  sliceState.m_execution.m_scaleoutCompletionWaitEvent);

    if (sliceState.m_isSend)
    {
        calculateScaleoutSendResources(sliceState, signalsManager);
    }
    else
    {
        calculateScaleoutRecvResources(sliceState, signalsManager);
    }
}

void Gen2ArchScaleoutProvider::requestScaleoutResources(NonCollectiveState& nonCollectiveState)
{
    LOG_HCL_TRACE(HCL,
                  "(NonCollectiveState): m_isSend={}, m_comm={}, m_isScaleoutRequired={}",
                  nonCollectiveState.m_isSend,
                  nonCollectiveState.m_comm,
                  nonCollectiveState.m_isScaleoutRequired);

    if (!nonCollectiveState.isScaleOutRequired())
    {
        LOG_HCL_TRACE(HCL, "Scale out is NOT required");
        return;
    }
}

void Gen2ArchScaleoutProvider::calculateScaleoutSendResources(SliceState& sliceState, SignalsManager& signalsManager)
{
    WaitEvent   waitEvent   = sliceState.m_execution.m_scaleoutCompletionWaitEvent;
    WaitMethod  waitMethod  = sliceState.m_execution.m_scaleoutCompletionWaitMethod;
    SignalEvent signalEvent = SignalEvent::SCALEOUT_SEND;

    sliceState.m_setup.m_scaleoutCompletionWaitSignal = signalEvent;

    switch (sliceState.m_currentOp)
    {
        case eHCLScatter:
        case eHCLSimpleBroadcast:
        case eHCLAllGather:
        case eHCLGather:
        case eHCLAll2All:
            signalsManager.enqueueWait(waitEvent, {signalEvent}, waitMethod);
            break;

        case eHCLReduceScatter:
        {
            WaitPhase phase = sliceState.m_syncUpBufferWithLtu ? 2 : 0;
            signalsManager.enqueueWait(waitEvent, {signalEvent}, waitMethod, phase);
            break;
        }

        case eHCLNoCollective:
            // Nothing to signal
            break;

        case eHCLBroadcast:
        case eHCLSinglePeerBroadcast:
        default:
            VERIFY(false);
    }
}

void Gen2ArchScaleoutProvider::calculateScaleoutRecvResources(SliceState& sliceState, SignalsManager& signalsManager)
{
    WaitEvent   waitEvent   = sliceState.m_execution.m_scaleoutCompletionWaitEvent;
    WaitMethod  waitMethod  = sliceState.m_execution.m_scaleoutCompletionWaitMethod;
    SignalEvent signalEvent = SignalEvent::SCALEOUT_RECV;

    sliceState.m_setup.m_scaleoutCompletionWaitSignal = signalEvent;

    switch (sliceState.m_currentOp)
    {
        case eHCLScatter:
        {
            if (sliceState.m_collectiveOp == eHCLBroadcast)
            {
                unsigned nextBox = getNextBox(sliceState.m_dynamicComm.getMyScaleupGroup(), sliceState.m_boxIterations);
                bool     isPeersOnly =
                    sliceState.m_isMultiScaleupGroup && sliceState.m_dynamicComm.getScaleupGroupSize() == 1;
                unsigned int numFences = (nextBox == sliceState.rootBox() || isPeersOnly) ? 1 : 2;

                signalsManager.enqueueWait(waitEvent, {signalEvent}, waitMethod, 0, numFences);
            }
            else  // eHCLSinglePeerBroadcast
            {
                signalsManager.enqueueWait(waitEvent, {signalEvent}, waitMethod);
            }
            break;
        }

        case eHCLSimpleBroadcast:
        case eHCLAllGather:
        case eHCLGather:
        case eHCLAll2All:
            signalsManager.enqueueWait(waitEvent, {signalEvent}, waitMethod);
            break;

        case eHCLReduceScatter:
        {
            unsigned longtermOffset      = 0;
            unsigned phaseOfWait         = 0;
            bool     expectAnotherPhase  = false;
            unsigned numOfExpectedFences = 1;
            if (isLongTerm(waitMethod))
            {
                if (sliceState.isRSContReduction())
                {
                    longtermOffset = sliceState.getScaleoutLongtermOffset(waitEvent);
                    phaseOfWait =
                        div(sliceState.m_boxIter, sliceState.m_scaleoutBuffersAmount * RS_CONT_REDUC_SO_POOL_AMOUNT);
                    expectAnotherPhase = sliceState.isAnotherPhaseWaitEventForFullBufferExpects();
                    if (mod(sliceState.m_boxIter, sliceState.m_scaleoutBuffersAmount) != 0)
                    {
                        numOfExpectedFences = 0;
                    }
                }
                else
                {
                    bool isEdgeIteration = sliceState.isEdgeIteration(sliceState.m_boxNumInfo);
                    longtermOffset       = isEdgeIteration ? sliceState.m_scaleoutLongtermAmount - 1
                                                           : (sliceState.calcBoxIterRecv(sliceState.m_boxNumInfo) %
                                                        sliceState.m_scaleoutBuffersAmount);
                    phaseOfWait          = isEdgeIteration ? 0
                                                           : sliceState.calcBoxIterRecv(sliceState.m_boxNumInfo) /
                                                        sliceState.m_scaleoutBuffersAmount;
                }
            }

            VERIFY(phaseOfWait < WAIT_PHASE_MAX, "phaseOfWait={}, WAIT_PHASE_MAX={}", phaseOfWait, WAIT_PHASE_MAX);
            signalsManager.enqueueWait(waitEvent,
                                       {signalEvent},
                                       waitMethod,
                                       phaseOfWait,
                                       numOfExpectedFences,
                                       longtermOffset,
                                       true,  // accSignals
                                       expectAnotherPhase);
            break;
        }

        case eHCLNoCollective:
            // Nothing to signal
            break;

        case eHCLBroadcast:
        case eHCLSinglePeerBroadcast:
        default:
            VERIFY(false);
    }
}

void Gen2ArchScaleoutProvider::openConnectionsOuterRanks(const HCL_Comm comm, const UniqueSortedVector& outerRanks)
{
    LOG_HCL_TRACE(HCL, "comm={}, outerRanks=[ {} ]", comm, outerRanks);
    m_device->openQpsScaleOut(comm, outerRanks);
}

void Gen2ArchScaleoutProvider::verifyConnections(HCL_Comm comm)
{
    UniqueSortedVector outerRanks;
    m_device->getOuterRanks(comm, outerRanks);
    for (auto& rank : outerRanks)
    {
        m_device->connectRankQps(comm, rank);
    }
}

void Gen2ArchScaleoutProvider::updateConnectionsNonPeer(
    const HCL_Comm                                          comm,
    const UniqueSortedVector&                               nonPeerRemoteRanks,
    [[maybe_unused]] const std::vector<HostNicConnectInfo>& bufferFromTargets)
{
    LOG_HCL_TRACE(HCL, "comm={}, nonPeerRemoteRanks.size={}", comm, nonPeerRemoteRanks.size());
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        m_device->connectRankQps(comm, remoteRank);
    }
}

void Gen2ArchScaleoutProvider::closeConnections([[maybe_unused]] HCL_Comm comm)
{
    // nothing to do here
    return;
}

unsigned Gen2ArchScaleoutProvider::getNumOfNicsPerDevice(const HCL_Comm comm) const
{
    return m_device->getComm(comm).getCommConnectivity().getNumScaleOutPorts();
}

LibfabricScaleoutProvider::LibfabricScaleoutProvider(HclDeviceGen2Arch* device)
: ScaleoutProvider(device), m_numArchStreams(device->getHal().getMaxStreams())
{
    LOG_HCL_INFO(HCL, "Scale-Out provider - libfabric");
    VERIFY(mod(m_numArchStreams, GCFG_HOST_SCHEDULER_THREADS.value()) == 0, "Invalid Number of Host Scheduler threads");

    m_hostStreamVec.resize(m_numArchStreams);
    uint64_t sizeOfHostBufferPool = 0;
    m_isGaudiDirect               = ofi_t::isGaudiDirect();

    MRParams mrParams;
    if (m_isGaudiDirect)
    {
        uint64_t scalBase, hbmPoolStart, allocatedSize;
        device->getScalManager().getHBMInfoForExport(scalBase, hbmPoolStart, allocatedSize);
        uint64_t offset = hbmPoolStart - scalBase;
        LOG_HCL_DEBUG(HCL,
                      "Mapping device memory: base addr 0x{:x} offset 0x{:x} size {:g}MB",
                      scalBase,
                      offset,
                      B2MB(allocatedSize));

        mrParams.m_fd     = device->getFd();
        mrParams.m_addr   = scalBase;
        mrParams.m_size   = allocatedSize;
        mrParams.m_offset = offset;

        VERIFY(mrParams.m_fd != 0, "HCL_GetDeviceFD returned 0 for device FD");
    }
    else
    {
        sizeOfHostBufferPool = device->getSIBBufferSize() * (HostBuffersAmount::getBufferCount(HNIC_SEND_POOL) +
                                                             HostBuffersAmount::getBufferCount(HNIC_RECV_POOL));
        uint64_t sizeOfAllHostBuffers = m_numArchStreams * sizeOfHostBufferPool;

        m_hostAddress = alloc_and_map_to_device(sizeOfAllHostBuffers,
                                                m_deviceHandle,
                                                device->getDeviceConfig().getFd(),
                                                nullptr,
                                                PROT_READ | PROT_WRITE,
                                                MAP_SHARED | MAP_ANONYMOUS);

        mrParams.m_addr = reinterpret_cast<uint64_t>(m_hostAddress);
        mrParams.m_size = sizeOfAllHostBuffers;
    }

    if (ofi_t::isMRLocal())
    {
        // create MemoryRegion.
        device->getOfiComponent()->initializeMemoryRegion(mrParams);
    }

    for (unsigned archStream = 0; archStream < m_numArchStreams; archStream++)
    {
        if (!isGaudiDirect())
        {
            m_hostBufferManager.push_back(
                new HostBufferManager(m_deviceHandle + (archStream * sizeOfHostBufferPool),
                                      (uint64_t)m_hostAddress + (archStream * sizeOfHostBufferPool),
                                      {{HNIC_SEND_POOL, HostBuffersAmount::getBufferCount(HNIC_SEND_POOL)},
                                       {HNIC_RECV_POOL, HostBuffersAmount::getBufferCount(HNIC_RECV_POOL)}},
                                      device->getSIBBufferSize()));
        }

        for (size_t uarchStream = 0;
             uarchStream < (GCFG_ENABLE_HNIC_MICRO_STREAMS.value() ? HOST_MICRO_ARCH_STREAMS : 1);
             uarchStream++)
        {
            const std::string uarchStreamStr = std::to_string(uarchStream);
            const std::string archStreamStr  = std::to_string(archStream);
            spHostStreamFifo  sendInternalQueue =
                std::make_shared<HostStreamFifo>("sendInternalQueue_" + uarchStreamStr);
            spHostStreamFifo recvInternalQueue =
                std::make_shared<HostStreamFifo>("recvInternalQueue_" + uarchStreamStr);

            m_hostStreamVec[archStream][uarchStream][HOST_STREAM_SEND] =
                new HostStream(archStreamStr + "_" + uarchStreamStr + "_HostSend",
                               archStream,
                               uarchStream,
                               sendInternalQueue,
                               HOST_STREAM_SEND);
            m_hostStreamVec[archStream][uarchStream][HOST_STREAM_RECV] =
                new HostStream(archStreamStr + "_" + uarchStreamStr + "_HostRecv",
                               archStream,
                               uarchStream,
                               recvInternalQueue,
                               HOST_STREAM_RECV);
            m_hostStreamVec[archStream][uarchStream][HOST_STREAM_WAIT_FOR_SEND_COMP] =
                new HostStream(archStreamStr + "_" + uarchStreamStr + "_HostSendWaitForCompletion",
                               archStream,
                               uarchStream,
                               sendInternalQueue,
                               HOST_STREAM_WAIT_FOR_SEND_COMP);
            m_hostStreamVec[archStream][uarchStream][HOST_STREAM_WAIT_FOR_RECV_COMP] =
                new HostStream(archStreamStr + "_" + uarchStreamStr + "_HostRecvWaitForCompletion",
                               archStream,
                               uarchStream,
                               recvInternalQueue,
                               HOST_STREAM_WAIT_FOR_RECV_COMP);
        }
    }

    m_streamsPerHostSched = m_hostStreamVec.size() / GCFG_HOST_SCHEDULER_THREADS.value();
    for (unsigned hostSchedId = 0; hostSchedId < GCFG_HOST_SCHEDULER_THREADS.value(); hostSchedId++)
    {
        m_hostScheduler.emplace_back(std::make_unique<HostScheduler>());

        std::vector<HostStream*> hostStreamVec;
        int                      archStream = m_streamsPerHostSched * hostSchedId;
        for (unsigned streams = 0; streams < m_streamsPerHostSched; streams++)
        {
            LOG_HCL_DEBUG(HCL,
                          "Host Scheduler id={} will process host streams of archStream={}",
                          hostSchedId,
                          archStream);
            for (size_t uarchStream = 0;
                 uarchStream < (GCFG_ENABLE_HNIC_MICRO_STREAMS.value() ? HOST_MICRO_ARCH_STREAMS : 1);
                 uarchStream++)
            {
                hostStreamVec.insert(hostStreamVec.end(),
                                     std::begin(m_hostStreamVec[archStream][uarchStream]),
                                     std::end(m_hostStreamVec[archStream][uarchStream]));
            }

            archStream++;
        }

        m_hostScheduler.at(hostSchedId)->startThread(device, hostSchedId, hostStreamVec);
    }
}

LibfabricScaleoutProvider::~LibfabricScaleoutProvider()
{
    for (unsigned i = 0; i < m_hostBufferManager.size(); i++)
    {
        delete m_hostBufferManager[i];
    }

    m_hostBufferManager.clear();
}

void LibfabricScaleoutProvider::destroy()
{
    for (unsigned hostSchedId = 0; hostSchedId < GCFG_HOST_SCHEDULER_THREADS.value(); hostSchedId++)
    {
        m_hostScheduler[hostSchedId]->stopThread();
    }
    if (!isGaudiDirect())
    {
        uint64_t sizeOfHostBufferPool =
            m_device->getSIBBufferSize() *
            (HostBuffersAmount::getBufferCount(HNIC_SEND_POOL) + HostBuffersAmount::getBufferCount(HNIC_RECV_POOL));
        uint64_t sizeOfAllHostBuffers = m_numArchStreams * sizeOfHostBufferPool;
        free_mem_mapped_to_device(m_hostAddress,
                                  sizeOfAllHostBuffers,
                                  m_deviceHandle,
                                  m_device->getDeviceConfig().getFd());
    }

    for (unsigned i = 0; i < m_hostBufferManager.size(); i++)
    {
        delete m_hostBufferManager[i];
    }
    m_hostBufferManager.clear();

    for (unsigned archStream = 0; archStream < m_numArchStreams; archStream++)
    {
        for (size_t uarchStream = 0;
             uarchStream < (GCFG_ENABLE_HNIC_MICRO_STREAMS.value() ? HOST_MICRO_ARCH_STREAMS : 1);
             uarchStream++)
        {
            for (int i = 0; i < NUM_HOST_STREAMS; i++)
            {
                delete m_hostStreamVec[archStream][uarchStream][i];
            }
        }
    }
    m_device->getOfiHandle()->releaseOfiComponent(m_device->getOfiDeviceId());
}

bool LibfabricScaleoutProvider::isHostNic() const
{
    return true;
}

bool LibfabricScaleoutProvider::isGaudiDirect() const
{
    return m_isGaudiDirect;
}

HostBufferManager* LibfabricScaleoutProvider::getHostBufferManager(unsigned streamIdx)
{
    return m_hostBufferManager.at(streamIdx);
}

void LibfabricScaleoutProvider::openConnectionsOuterRanks(const HCL_Comm comm, const UniqueSortedVector& outerRanks)
{
    LOG_HCL_TRACE(HCL, "comm={}, outerRanks=[ {} ]", comm, outerRanks);
    HclDynamicCommunicator& dynamicComm = m_device->getComm(comm);
    if (nullptr == dynamicComm.m_hostNicBridge)
    {
        dynamicComm.m_hostNicBridge = std::make_unique<ofi_communicator>();
    }

    VERIFY(dynamicComm.initializeHostNicBridge(outerRanks));
    for (const HCL_Rank rank : outerRanks)
    {
        m_device->updateRankHasQp(comm, rank);
    }
}

void LibfabricScaleoutProvider::verifyConnections(HCL_Comm comm)
{
    HclDynamicCommunicator& dynamicComm = m_device->getComm(comm);

    UniqueSortedVector outerRanks;
    m_device->getOuterRanks(comm, outerRanks);
    LOG_HCL_TRACE(HCL, "comm={}, outerRanks=[ {} ]", comm, outerRanks);

    bool res;
    for (auto& rank : outerRanks)
    {
        res =
            dynamicComm.m_hostNicBridge->updateConnections(dynamicComm.m_remoteDevices[rank]->header.hcclRank,
                                                           dynamicComm.m_remoteDevices[rank]->remoteInfo.hostNicConns);
        VERIFY(res == true, "Failed to update connection to rank {}", rank);
    }
}

SignalEvent LibfabricScaleoutProvider::getScaleoutSendSignal()
{
    return SignalEvent::HNIC_SCALEOUT_SEND;
}

SignalEvent LibfabricScaleoutProvider::getScaleoutRecvSignal(bool doReduction)
{
    return isGaudiDirect() ? (doReduction ? SignalEvent::EDMA_MEMCOPY_GDR : SignalEvent::HNIC_SCALEOUT_RECV)
                           : SignalEvent::HNIC_PDMA;
}

int LibfabricScaleoutProvider::getInternalScaleoutFences()
{
    return 1;
}

void LibfabricScaleoutProvider::validateSize(uint64_t size)
{
    VERIFY(!(!isGaudiDirect() && size > m_device->getSIBBufferSize()),
           "Maximal Transaction size in Host NIC (non GDR) is defined by HCL_IMB_SIZE ({}B)",
           m_device->getSIBBufferSize());
}

void LibfabricScaleoutProvider::updateConnectionsNonPeer(const HCL_Comm                         comm,
                                                         const UniqueSortedVector&              nonPeerRemoteRanks,
                                                         const std::vector<HostNicConnectInfo>& bufferFromTargets)
{
    LOG_HCL_TRACE(HCL,
                  "comm={}, nonPeerRemoteRanks.size={}, bufferFromTargets.size={}",
                  comm,
                  nonPeerRemoteRanks.size(),
                  bufferFromTargets.size());
    VERIFY(nonPeerRemoteRanks.size() == bufferFromTargets.size());
    HclDynamicCommunicator& dynamicComm = m_device->getComm(comm);

    size_t ranksCounter = 0;
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        const HostNicConnectInfo& bufferFromTarget(bufferFromTargets[ranksCounter++]);

        const bool res = dynamicComm.m_hostNicBridge->updateConnections(remoteRank, bufferFromTarget);
        VERIFY(res, "updateConnection failed, remoteRank={}", remoteRank);
    }
}

void LibfabricScaleoutProvider::closeConnections(HCL_Comm comm)
{
    HclDynamicCommunicator& dynamicComm = m_device->getComm(comm);
    dynamicComm.m_hostNicBridge->destroy();
}

int LibfabricScaleoutProvider::setInternalScaleoutRecvWait(WaitMethod      method,
                                                           SignalsManager& signalsManager,
                                                           bool            doReduction)
{
    if (!isGaudiDirect())
    {
        signalsManager.enqueueWait(WaitEvent::HNIC_SCALEOUT_RECV_PDMA_WAIT_FOR_RECV,
                                   {SignalEvent::HNIC_SCALEOUT_RECV},
                                   method,
                                   signalsManager.getNextPhase(method));
        return 1;  // single internal wait
    }
    else if (doReduction)
    {
        signalsManager.enqueueWait(WaitEvent::GDR_MEMCPY_WAIT_FOR_HNIC_RECV,
                                   {SignalEvent::HNIC_SCALEOUT_RECV},
                                   method,
                                   signalsManager.getNextPhase(method));
        return 1;  // single internal wait
    }

    return 0;
}

void LibfabricScaleoutProvider::requestScaleoutResources(SliceState& sliceState, SignalsManager& signalsManager)
{
    WaitEvent  waitEvent  = sliceState.m_execution.m_scaleoutCompletionWaitEvent;
    WaitMethod waitMethod = sliceState.m_execution.m_scaleoutCompletionWaitMethod;

    LOG_HCL_TRACE(HCL,
                  "requesting resources for currentOp {} (will signal to {} using event {})",
                  sliceState.m_currentOp,
                  waitMethod,
                  waitEvent);

    switch (sliceState.m_collectiveOp)
    {
        case eHCLAllReduce:
        case eHCLReduceScatter:
        case eHCLAll2All:
        case eHCLBroadcast:
        case eHCLSinglePeerBroadcast:
        case eHCLAllGather:
        case eHCLSimpleBroadcast:
        case eHCLReduce:
        case eHCLNoCollective:
            /* supported*/
            break;

        case eHCLGather:
        case eHCLScatter:
            VERIFY(false, "Not supported yet");
            break;
        case eHCLCollectiveLastValue:
            VERIFY(false, "Invalid collective");
            break;
    }

    if (!sliceState.isScaleoutRequired(sliceState.m_isSend, sliceState.m_boxNumInfo))
    {
        LOG_HCL_TRACE(HCL, "Scale out is NOT required");
        return;
    }

    sliceState.m_setup.m_scaleoutInternalFences = 1;

    if (sliceState.m_isSend)
    {
        SignalEvent signalEvent                           = SignalEvent::HNIC_SCALEOUT_SEND;
        sliceState.m_setup.m_scaleoutCompletionWaitSignal = signalEvent;

        WaitPhase phase = sliceState.m_currentOp == eHCLReduceScatter ? (sliceState.m_syncUpBufferWithLtu ? 2 : 0) : 0;
        signalsManager.enqueueWait(waitEvent, {signalEvent}, waitMethod, phase);
    }
    else  // recv
    {
        sliceState.m_setup.m_scaleoutInternalSOBs = isGaudiDirect() ? 0 : 1;

        // Gaudi-direct doesn't involve PDMA operation, so the signaling event is the scaleout recv
        SignalEvent signalEvent = isGaudiDirect() ? SignalEvent::HNIC_SCALEOUT_RECV : SignalEvent::HNIC_PDMA;
        sliceState.m_setup.m_scaleoutCompletionWaitSignal = signalEvent;

        if (sliceState.m_currentOp == eHCLReduceScatter)
        {
            unsigned  longtermOffset      = 0;
            bool      expectAnotherPhase  = false;
            unsigned  numOfExpectedFences = 1;
            WaitPhase waitPhase           = 0;
            if (waitEvent == WaitEvent::HNIC_SIGNAL_SPLIT_WAIT_FOR_PDMA)
            {
                waitPhase = 1;
            }
            else if (sliceState.isRSContReduction())
            {
                longtermOffset = sliceState.getScaleoutLongtermOffset(waitEvent);
                waitPhase =
                    div(sliceState.m_boxIter, sliceState.m_scaleoutBuffersAmount * RS_CONT_REDUC_SO_POOL_AMOUNT);
                if (sliceState.m_boxIter + sliceState.m_scaleoutBuffersAmount + 1 < sliceState.m_boxIterations)
                {
                    expectAnotherPhase = true;
                }
                if (mod(sliceState.m_boxIter, sliceState.m_scaleoutBuffersAmount) != 0)
                {
                    numOfExpectedFences = 0;
                }
            }
            signalsManager.enqueueWait(waitEvent,
                                       {signalEvent},
                                       waitMethod,
                                       waitPhase,
                                       numOfExpectedFences,
                                       longtermOffset,
                                       true,
                                       expectAnotherPhase);
        }
        else if (sliceState.m_collectiveOp == eHCLBroadcast && sliceState.m_currentOp == eHCLScatter)
        {
            bool isPeersOnly = sliceState.m_isMultiScaleupGroup && sliceState.m_dynamicComm.getScaleupGroupSize() == 1;
            unsigned nextBox = getNextBox(sliceState.m_dynamicComm.getMyScaleupGroup(), sliceState.m_boxIterations);

            WaitPhase    waitPhase = isGaudiDirect() || (isPeersOnly && nextBox == sliceState.rootBox()) ? 0 : 1;
            unsigned int numFences = 1;
            if (isGaudiDirect() && (nextBox != sliceState.rootBox()))
            {
                numFences = 2;
            }
            signalsManager.enqueueWait(waitEvent, {signalEvent}, waitMethod, waitPhase, numFences);
        }
        else
        {
            signalsManager.enqueueWait(waitEvent, {signalEvent}, waitMethod);
        }
    }
}

void LibfabricScaleoutProvider::requestScaleoutResources(NonCollectiveState& nonCollectiveState)
{
    LOG_HCL_TRACE(HCL,
                  "(NonCollectiveState): m_isSend={}, m_comm={}, m_isScaleoutRequired={}",
                  nonCollectiveState.m_isSend,
                  nonCollectiveState.m_comm,
                  nonCollectiveState.m_isScaleoutRequired);

    if (!nonCollectiveState.isScaleOutRequired())
    {
        LOG_HCL_TRACE(HCL, "Scale out is NOT required");
        return;
    }

    nonCollectiveState.m_setup.m_scaleoutInternalFences = 1;
    nonCollectiveState.m_setup.m_scaleoutInternalSOBs   = isGaudiDirect() ? 0 : 1;
}

void LibfabricScaleoutProvider::notifyHostScheduler(int archStreamIdx)
{
    int hostSchedIndex = archStreamIdx / m_streamsPerHostSched;
    return m_hostScheduler[hostSchedIndex]->notifyThread();
}
