#include "platform/gaudi2/hcl_device_controller.h"
#include "platform/gaudi2/hcl_graph_sync.h"
#include "platform/gen2_arch_common/hcl_device.h"
#include "platform/gaudi2/commands/hcl_commands.h"  // for HclCommandsGaudi2
#include "infra/scal/gaudi2/scal_manager.h"         // for Gaudi2S...
#include "infra/scal/gaudi2/stream_layout.h"
#include "infra/scal/gen2_arch_common/scal_names.h"       // for ScalJsonNames
#include "platform/gen2_arch_common/hcl_packets_utils.h"  // for getCompCfg
#include "platform/gaudi2/hcl_device.h"

HclDeviceControllerGaudi2::HclDeviceControllerGaudi2(const int fd, const unsigned numOfStreams)
: HclDeviceControllerGaudiCommon(numOfStreams)
{
    m_commands     = std::make_unique<HclCommandsGaudi2>();
    m_streamLayout = std::make_unique<Gaudi2StreamLayout>();
    m_scalManager  = std::make_unique<hcl::Gaudi2ScalManager>(fd, *m_commands, *m_streamLayout);

    for (unsigned i = 0; i < m_numOfStreams; i++)
    {
        m_streamSyncParams[i].m_smInfo = m_scalManager->getSmInfo(i);
        m_graphSync[i] = std::make_unique<HclGraphSyncGaudi2>(m_streamSyncParams[i].m_smInfo.soSmIndex, *m_commands);
    }
}

void HclDeviceControllerGaudi2::initGlobalContext(HclDeviceGen2Arch* device, uint8_t apiId)
{
    LOG_HCL_DEBUG(HCL_SCAL, "HCL initializes ScalManager Global Context...");
    hcl::Gaudi2ScalManager* gaudi2ScalManager = dynamic_cast<hcl::Gaudi2ScalManager*>(m_scalManager.get());

    hcl::Gen2ArchScalWrapper::CgComplex cgComplex =
        gaudi2ScalManager->getCgInfo("network_scaleup_init_completion_queue");

    // Add network_scaleup_init_completion_queue to completion config
    const unsigned queue_id = hcl::ScalJsonNames::numberOfArchsStreams * 2;
    getCompCfg()[queue_id]  = SoBaseAndSize(cgComplex.cgInfo.cgBaseAddr, cgComplex.cgInfo.size);

    hcl::ScalStream& recvStream   = getScalStream(0, HclStreamIndex::SU_RECV_RS);
    hcl::ScalStream& recvSOStream = getScalStream(0, HclStreamIndex::SO_RECV_RS);
    hcl::ScalStream& dmaStream    = getScalStream(0, HclStreamIndex::GP_ARB);

    recvStream.setTargetValue(0);
    recvSOStream.setTargetValue(0);
    dmaStream.setTargetValue(0);

    const std::vector<SimbPoolContainerParamsPerStream>& containerParamsPerStreamVec =
        gaudi2ScalManager->getContainerParamsPerStreamVec();
    VERIFY(containerParamsPerStreamVec.size(), "Uninitialized static buffer addresses and sizes");

    this->serializeInitSequenceCommands(recvStream,
                                        recvSOStream,
                                        dmaStream,
                                        cgComplex.cgInfo.cgIdx[hcl::SchedulersIndex::recvScaleUp],
                                        cgComplex.cgInfo.cgBaseAddr,
                                        containerParamsPerStreamVec,
                                        device,
                                        apiId);

    LOG_HCL_TRACE(HCL_SCAL,
                  "serializeUpdateGlobalContextCommand on cg 0x{:x}, cg index: {}, longSO=0x{:x}",
                  (uint64_t)cgComplex.cgHandle,
                  cgComplex.cgInfo.cgIdx[hcl::SchedulersIndex::recvScaleUp],
                  (uint64_t)cgComplex.cgInfo.longSoAddr);

    recvStream.submit();
    recvSOStream.submit();
    dmaStream.submit();

    // Wait for completion
    gaudi2ScalManager->waitOnCg(cgComplex, 1);
    LOG_HCL_DEBUG(HCL_SCAL, "HCL initialized ScalManager Global Context");
}

void HclDeviceControllerGaudi2::serializeInitSequenceCommands(
    hcl::ScalStreamBase&                                 recvStream,
    hcl::ScalStreamBase&                                 recvSOStream,
    hcl::ScalStreamBase&                                 dmaStream,
    unsigned                                             indexOfCg,
    uint64_t                                             soAddressLSB,
    const std::vector<SimbPoolContainerParamsPerStream>& containerParamsPerStreamVec,
    HclDeviceGen2Arch*                                   device,
    uint8_t                                              apiId)
{
    uint64_t fwBaseAddress = 0;
    unsigned fwSliceSize   = 0;

    fwBaseAddress = device->getDeviceConfig().getSramBaseAddress();
    VERIFY(GCFG_FW_IMB_SIZE.value() <= device->getDeviceConfig().getHclReservedSramSize(),
           "FW IMB is located on SRAM, cannot be bigger than HCL reserved SRAM size.");
    fwSliceSize = GCFG_FW_IMB_SIZE.value();

    HclCommandsGaudi2& gaudi2Commands = (HclCommandsGaudi2&)getGen2ArchCommands();
    gaudi2Commands.serializeInitSequenceCommands(recvStream,
                                                 recvSOStream,
                                                 dmaStream,
                                                 indexOfCg,
                                                 soAddressLSB,
                                                 containerParamsPerStreamVec,
                                                 ((HclDeviceGaudi2*)m_device)->getContextManager(),
                                                 fwSliceSize,
                                                 fwBaseAddress,
                                                 apiId);
}