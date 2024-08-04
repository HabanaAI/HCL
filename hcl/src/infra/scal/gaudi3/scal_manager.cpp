#include "infra/scal/gaudi3/scal_manager.h"

#include <cstdint>  // for uint64_t
#include "infra/scal/gaudi3/scal_utils.h"
#include "infra/scal/gaudi3/scal_wrapper.h"  // for Gaudi3ScalWrapper
#include "infra/scal/gen2_arch_common/scal_exceptions.h"
#include "infra/scal/gen2_arch_common/scal_wrapper.h"  // for Gen2ArchScalWr...
#include "platform/gaudi3/commands/hcl_commands.h"     // for HclCommandsGaudi3
#include "platform/gaudi3/hcl_device.h"                // for HclDeviceGaudi3
#include "platform/gaudi3/hcl_graph_sync.h"
#include "platform/gaudi3/commands/hcl_commands.h"
#include "platform/gen2_arch_common/intermediate_buffer_container.h"  // for getAllBufferBaseAddr, getSliceSize
#include "hcl_api_types.h"
#include "infra/scal/gaudi3/arch_stream.h"
#include "hcl_math_utils.h"
#include "platform/gen2_arch_common/hcl_packets_utils.h"  // for getCompCfg
class HclCommandsGen2Arch;  // lines 9-9
class HclDeviceGen2Arch;    // lines 10-10

const hcl::SchedulersIndex initCgSchedList[] = {hcl::SchedulersIndex::sendScaleUp, hcl::SchedulersIndex::recvScaleUp, hcl::SchedulersIndex::dma};

namespace hcl
{
class ScalStreamBase;
}

using namespace hcl;

Gaudi3ScalManager::Gaudi3ScalManager(int fd, HclCommandsGen2Arch& commands) : Gen2ArchScalManager(fd, commands)
{
    if (fd == -1) return;
    m_scalWrapper.reset(new Gaudi3ScalWrapper(fd, m_scalNames));
    init();
}

Gaudi3ScalManager::~Gaudi3ScalManager() {}

void Gaudi3ScalManager::initSimb(HclDeviceGen2Arch* device, uint8_t apiID)
{
    HclCommandsGaudi3& gaudi3Commands = (HclCommandsGaudi3&)((HclDeviceGaudi3*)device)->getGen2ArchCommands();
    HclGraphSyncGaudi3 graphSync(0, gaudi3Commands);
    Gen2ArchScalWrapper::CgComplex cgComplex    = m_scalWrapper->getCgInfo("network_scaleup_init_completion_queue");
    uint64_t soAddressLSB = cgComplex.cgInfo.cgBaseAddr + (mod(++m_configurationCount, cgComplex.cgInfo.size) * 4);
    hcl::ScalStream&               dmaStream    = getScalStream(0, (unsigned)hcl::SchedulersIndex::dma, 2);
    dmaStream.setTargetValue(0);

    uint64_t fwBaseAddress = device->m_sibContainer->getFwBaseAddr();
    unsigned fwSliceSize   = device->m_sibContainer->getFwSliceSize();

    // Add network_scaleup_init_completion_queue to completion config
    const unsigned queue_id = m_archStreams.size() * 2;
    getCompCfg()[queue_id]  = SoBaseAndSize(cgComplex.cgInfo.cgBaseAddr, cgComplex.cgInfo.size);

    // Alloc Barrier
    for (auto sched : initCgSchedList)
    {
        unsigned&        cgIdx    = cgComplex.cgInfo.cgIdx[(int)sched];
        hcl::ScalStream& abStream = getScalStream(0, (unsigned)sched, 2);
        gaudi3Commands.serializeAllocBarrierCommand(abStream, (int)sched, cgIdx, 1);
    }

    // Set the SO to the correct value 0x4000-numberOfSignals
    unsigned numberOfSignals = m_staticBufferAddressesAndSizes.size() * device->getEdmaEngineWorkDistributionSize();
    gaudi3Commands.serializeLbwWriteCommand(
        dmaStream,
        (unsigned)hcl::SchedulersIndex::recvScaleUp,
        soAddressLSB,
        graphSync.getSoConfigValue(COMP_SYNC_GROUP_CMAX_TARGET - numberOfSignals, true));
    LOG_HCL_TRACE(HCL,
                  "RR | intermediateBaseAddressFirstPool 0x{:x}, slice size: {:g}MB, "
                  "intermediateBaseAddressSecondPool 0x{:x}, slice size: {:g}MB, fwBaseAddress 0x{:x}, FW "
                  "slice size: {:g}MB",
                  uint64_t(m_staticBufferAddressesAndSizes.at(0).sibBaseAddr),
                  B2MB(m_staticBufferAddressesAndSizes.at(0).sibSize),
                  uint64_t(m_staticBufferAddressesAndSizes.at(1).sibBaseAddr),
                  B2MB(m_staticBufferAddressesAndSizes.at(1).sibSize),
                  uint64_t(fwBaseAddress),
                  B2MB(fwSliceSize));
    gaudi3Commands.serializeGlobalDmaCommand(dmaStream,
                                             soAddressLSB & 0xffffffff,
                                             m_staticBufferAddressesAndSizes,
                                             fwSliceSize,
                                             fwBaseAddress);

    // Add memset commands to the cyclic buffer
    uint8_t streamCtxtID = hcl::encodeStreamContextID(apiID, hcl::DEFAULT_STREAM_IDX);
    for (auto& addrAndSize : m_staticBufferAddressesAndSizes)
    {
        gaudi3Commands.serializeMemsetCommand(dmaStream,
                                              (unsigned)hcl::SchedulersIndex::dma,
                                              addrAndSize.sibBaseAddr,
                                              addrAndSize.sibSize * addrAndSize.sibAmount,
                                              soAddressLSB & 0xffffffff,
                                              streamCtxtID,
                                              hcclFloat32,
                                              hcclSum);  // hcclSum -> memset to 0
    }

    // Submit to FW
    dmaStream.submit();

    // Wait for completion
    Gen2ArchScalManager::waitOnCg(cgComplex, m_configurationCount + 1);
}

void Gaudi3ScalManager::configScaleupQps(HCL_Comm comm, HclDeviceGaudi3* device, bool isSend)
{
    LOG_HCL_DEBUG(HCL_SCAL,
                  "configuring {} Qps, m_configurationCount={}",
                  isSend ? "send" : "recv",
                  m_configurationCount);

    HclCommandsGaudi3&   gaudi3Commands = (HclCommandsGaudi3&)(device->getGen2ArchCommands());
    HclGraphSyncGaudi3   graphSync(0, gaudi3Commands);
    hcl::SchedulersIndex sched = isSend ? hcl::SchedulersIndex::sendScaleUp : hcl::SchedulersIndex::recvScaleUp;
    Gen2ArchScalWrapper::CgComplex cgComplex    = m_scalWrapper->getCgInfo("network_scaleup_init_completion_queue");
    uint64_t soAddressLSB = cgComplex.cgInfo.cgBaseAddr + (mod(++m_configurationCount, cgComplex.cgInfo.size) * 4);

    constexpr unsigned qpArchStreamIdx = 0;
    hcl::ScalStream&   stream          = getScalStream(qpArchStreamIdx, (unsigned)sched, 2);
    stream.setTargetValue(0);

    // Alloc Barrier
    for (auto sched : initCgSchedList)
    {
        unsigned&        cgIdx    = cgComplex.cgInfo.cgIdx[(int)sched];
        hcl::ScalStream& abStream = getScalStream(qpArchStreamIdx, (unsigned)sched, 2);
        gaudi3Commands.serializeAllocBarrierCommand(abStream, (int)sched, cgIdx, 1);
    }

    // set the SO to the correct value 0x400-0x1
    gaudi3Commands.serializeLbwWriteCommand(stream,
                                            (unsigned)sched,
                                            soAddressLSB,
                                            graphSync.getSoConfigValue(COMP_SYNC_GROUP_CMAX_TARGET - 1, true));

    // for null submission, disable Scal write for QP's config since they are 0
    if (GCFG_HCL_NULL_SUBMIT.value())
    {
        LOG_HCL_TRACE(HCL_SCAL, "calling disableCcb(true)");
        disableCcb(qpArchStreamIdx, true);
    }

    // add the RS qp configuration commands to the cyclic buffer
    device->m_qpManagerScaleUp->setNicOffsets(stream, device, comm, eHCLReduceScatter, isSend);
    device->m_qpManagerScaleUp->setLastRankScaleup(stream, device, comm, eHCLReduceScatter, isSend);

    // add the AG qp configuration commands to the cyclic buffer
    device->m_qpManagerScaleUp->setNicOffsets(stream, device, comm, eHCLAllGather, isSend);
    device->m_qpManagerScaleUp->setLastRankScaleup(stream, device, comm, eHCLAllGather, isSend);

    // add the A2A qp configuration commands to the cyclic buffer
    device->m_qpManagerScaleUp->setNicOffsets(stream, device, comm, eHCLAll2All, isSend);
    device->m_qpManagerScaleUp->setLastRankScaleup(stream, device, comm, eHCLAll2All, isSend);

    if (GCFG_HCL_NULL_SUBMIT.value())
    {
        LOG_HCL_TRACE(HCL_SCAL, "calling disableCcb(false)");
        disableCcb(qpArchStreamIdx, false);
    }

    // Increment the SO to free the barrier
    gaudi3Commands.serializeLbwWriteCommand(stream, (unsigned)sched, soAddressLSB, graphSync.getSoConfigValue(1, true));

    // submit to FW
    stream.submit();

    // Wait for completion
    Gen2ArchScalManager::waitOnCg(cgComplex, m_configurationCount + 1);
}

void Gaudi3ScalManager::configQps(HCL_Comm comm, HclDeviceGen2Arch* device)
{
    if (device->getComm(comm).isCommunicatorMultiScaleupGroup() &&
        device->getComm(comm).isCommunicatorScaleupGroupPeers())
    {
        LOG_HCL_DEBUG(HCL_SCAL, "comm {} is Scaleout only peers, will not add scaleup QPs", comm);
        return;
    }

    configScaleupQps(comm, (HclDeviceGaudi3*)device, true);
    configScaleupQps(comm, (HclDeviceGaudi3*)device, false);
}

void Gaudi3ScalManager::init()
{
    Gen2ArchScalManager::init();

    for (size_t i = 0; i < m_archStreams.size(); i++)
    {
        scal_comp_group_handle_t internalCgHandle = m_cgInfoArray[i][(int)SchedulerType::internal].cgHandle;
        scal_comp_group_handle_t externalCgHandle = m_cgInfoArray[i][(int)SchedulerType::external].cgHandle;
        m_archStreams[i]                          = std::unique_ptr<ArchStream>(
            new Gaudi3ArchStream(i, *m_scalWrapper, externalCgHandle, internalCgHandle, m_scalNames, m_commands));
    }
}