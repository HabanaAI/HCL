#include "platform/gaudi3/hcl_device_controller.h"
#include "platform/gaudi3/hcl_graph_sync.h"
#include "platform/gen2_arch_common/hcl_device.h"
#include "platform/gen2_arch_common/wqe_tracker.h"
#include "platform/gaudi3/commands/hcl_commands.h"  // for HclCommandsGaudi3
#include "infra/scal/gaudi3/scal_manager.h"
#include "infra/scal/gaudi3/stream_layout.h"
#include "infra/scal/gen2_arch_common/scal_wrapper.h"     // for Gen2ArchScalWrapper
#include "platform/gen2_arch_common/hcl_packets_utils.h"  // for getCompCfg
#include "infra/scal/gen2_arch_common/scal_names.h"       // for ScalJsonNames

HclDeviceControllerGaudi3::HclDeviceControllerGaudi3(const int fd, const unsigned numOfStreams)
: HclDeviceControllerGaudiCommon(numOfStreams)
{
    m_commands     = std::make_unique<HclCommandsGaudi3>();
    m_streamLayout = std::make_unique<Gaudi3StreamLayout>();
    m_scalManager  = std::make_unique<hcl::Gaudi3ScalManager>(fd, *m_commands, *m_streamLayout);

    for (unsigned i = 0; i < m_numOfStreams; i++)
    {
        m_streamSyncParams[i].m_smInfo = m_scalManager->getSmInfo(i);
        m_graphSync[i] = std::make_unique<HclGraphSyncGaudi3>(m_streamSyncParams[i].m_smInfo.soSmIndex, *m_commands);
    }
}

void HclDeviceControllerGaudi3::clearSimb(HclDeviceGen2Arch* device, uint8_t apiID)
{
    HclCommandsGaudi3&                  gaudi3Commands    = (HclCommandsGaudi3&)getGen2ArchCommands();
    HclGraphSyncGaudi3&                 graphSync         = (HclGraphSyncGaudi3&)getGraphSync(0);
    hcl::Gaudi3ScalManager*             gaudi3ScalManager = dynamic_cast<hcl::Gaudi3ScalManager*>(m_scalManager.get());
    hcl::Gen2ArchScalWrapper::CgComplex cgComplex =
        gaudi3ScalManager->getCgInfo("network_scaleup_init_completion_queue");
    uint64_t soAddressLSB = gaudi3ScalManager->getInitCgNextSo();

    hcl::ScalStream& dmaStream = getScalStream(0, HclStreamIndex::GP_ARB);
    dmaStream.setTargetValue(0);

    uint64_t fwBaseAddress = device->m_sibContainerManager->getFwBaseAddr();
    unsigned fwSliceSize   = device->m_sibContainerManager->getFwSliceSize();

    // Add network_scaleup_init_completion_queue to completion config
    const unsigned queue_id = hcl::ScalJsonNames::numberOfArchsStreams * 2;
    getCompCfg()[queue_id]  = SoBaseAndSize(cgComplex.cgInfo.cgBaseAddr, cgComplex.cgInfo.size);

    // Alloc Barrier
    for (auto sched : initCgSchedList)
    {
        unsigned&        cgIdx    = cgComplex.cgInfo.cgIdx[(int)sched];
        hcl::ScalStream& abStream = getScalArbUarchStream(0, sched);
        gaudi3Commands.serializeAllocBarrierCommand(abStream, (int)sched, cgIdx, 1);
    }

    // Set the SO to the correct value 0x4000-numberOfSignals
    const std::vector<SimbPoolContainerParamsPerStream>& containerParamsPerStreamVec =
        gaudi3ScalManager->getContainerParamsPerStreamVec();
    unsigned numberOfSignals = containerParamsPerStreamVec.size() * device->getEdmaEngineWorkDistributionSize();
    gaudi3Commands.serializeLbwWriteCommand(
        dmaStream,
        dmaStream.getSchedIdx(),
        soAddressLSB,
        graphSync.getSoConfigValue(COMP_SYNC_GROUP_CMAX_TARGET - numberOfSignals, true));

    for (int i = 0; i < MAX_POOL_CONTAINER_IDX; i++)
    {
        LOG_HCL_TRACE(HCL,
                      "container{}: 0x{:x}, slice size: {:g}MB",
                      i,
                      uint64_t(containerParamsPerStreamVec.at(i).m_streamBaseAddrInContainer),
                      B2MB(containerParamsPerStreamVec.at(i).m_simbSize));
    }
    LOG_HCL_TRACE(HCL, "fwBaseAddress 0x{:x}, FW slice size: {:g}MB", uint64_t(fwBaseAddress), B2MB(fwSliceSize));
    gaudi3Commands.serializeGlobalDmaCommand(dmaStream,
                                             dmaStream.getSchedIdx(),
                                             soAddressLSB & 0xffffffff,
                                             containerParamsPerStreamVec,
                                             fwSliceSize,
                                             fwBaseAddress);

    // Add memset commands to the cyclic buffer
    uint8_t streamCtxtID = getEdmaStreamCtxtId(apiID, hcl::DEFAULT_STREAM_IDX);
    for (auto& addrAndSize : containerParamsPerStreamVec)
    {
        gaudi3Commands.serializeMemsetCommand(dmaStream,
                                              dmaStream.getSchedIdx(),
                                              addrAndSize.m_streamBaseAddrInContainer,
                                              addrAndSize.m_simbSize * addrAndSize.m_simbCountPerStream,
                                              soAddressLSB & 0xffffffff,
                                              streamCtxtID,
                                              hcclFloat32,
                                              hcclSum);  // hcclSum -> memset to 0
    }

    // Submit to FW
    dmaStream.submit();

    // Wait for completion
    gaudi3ScalManager->waitOnCg(cgComplex, gaudi3ScalManager->getConfigurationCount() + 1);
}