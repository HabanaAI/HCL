#include "infra/scal/gaudi2/scal_manager.h"

#include <cstdint>  // for uint64_t
#include "hcl_utils.h"
#include "infra/scal/gaudi2/scal_wrapper.h"            // for Gaudi2ScalWrapper
#include "infra/scal/gen2_arch_common/scal_wrapper.h"  // for Gen2ArchScalWr...
#include "platform/gaudi2/commands/hcl_commands.h"     // for HclCommandsGaudi2
#include "platform/gaudi2/hcl_device.h"                // for HclDeviceGaudi2
#include "platform/gen2_arch_common/hcl_packets_utils.h"  // for getCompCfg
#include "infra/scal/gen2_arch_common/scal_exceptions.h"
#include "infra/scal/gaudi2/arch_stream.h"
#include "platform/gen2_arch_common/intermediate_buffer_container.h"

class HclCommandsGen2Arch;
class HclDeviceGen2Arch;
namespace hcl
{
class ScalStreamBase;
}

using namespace hcl;

Gaudi2ScalManager::Gaudi2ScalManager(int fd, HclCommandsGen2Arch& commands) : Gen2ArchScalManager(fd, commands)
{
    if (fd == -1) return;
    m_scalWrapper.reset(new Gaudi2ScalWrapper(fd, m_scalNames));
    init();
}

Gaudi2ScalManager::~Gaudi2ScalManager() {}

void Gaudi2ScalManager::serializeInitSequenceCommands(hcl::ScalStreamBase&                  recvStream,
                                                      hcl::ScalStreamBase&                  recvSOStream,
                                                      hcl::ScalStreamBase&                  dmaStream,
                                                      unsigned                              indexOfCg,
                                                      uint64_t                              soAddressLSB,
                                                      const std::vector<sibAddressAndSize>& sibAddressesAndSizes,
                                                      HclDeviceGen2Arch*                    device,
                                                      uint8_t                               apiId)
{
    uint64_t fwBaseAddress = 0;
    unsigned fwSliceSize   = 0;

    fwBaseAddress = device->getDeviceConfig().getSramBaseAddress();
    VERIFY(GCFG_FW_IMB_SIZE.value() <= device->getDeviceConfig().getHclReservedSramSize(),
           "FW IMB is located on SRAM, cannot be bigger than HCL reserved SRAM size.");
    fwSliceSize = GCFG_FW_IMB_SIZE.value();

    ((HclCommandsGaudi2&)(((HclDeviceGaudi2*)device)->getGen2ArchCommands()))
        .serializeInitSequenceCommands(recvStream,
                                       recvSOStream,
                                       dmaStream,
                                       indexOfCg,
                                       soAddressLSB,
                                       sibAddressesAndSizes,
                                       ((HclDeviceGaudi2*)device)->getContextManager(),
                                       fwSliceSize,
                                       fwBaseAddress,
                                       apiId);
}

void Gaudi2ScalManager::initGlobalContext(HclDeviceGen2Arch* device, uint8_t apiId)
{
    LOG_HCL_DEBUG(HCL_SCAL, "HCL initializes ScalManager Global Context...");

    Gen2ArchScalWrapper::CgComplex cgComplex = m_scalWrapper->getCgInfo("network_scaleup_init_completion_queue");

    // Add network_scaleup_init_completion_queue to completion config
    const unsigned queue_id = m_archStreams.size() * 2;
    getCompCfg()[queue_id]  = SoBaseAndSize(cgComplex.cgInfo.cgBaseAddr, cgComplex.cgInfo.size);

    hcl::ScalStream& recvStream   = getScalStream(0, (unsigned)SchedulersIndex::recvScaleUp, 0);
    hcl::ScalStream& recvSOStream = getScalStream(0, (unsigned)SchedulersIndex::recvScaleOut, 0);
    hcl::ScalStream& dmaStream    = getScalStream(0, (unsigned)SchedulersIndex::dma, 0);

    recvStream.setTargetValue(0);
    recvSOStream.setTargetValue(0);
    dmaStream.setTargetValue(0);

    VERIFY(m_staticBufferAddressesAndSizes.size(), "Uninitialized static buffer addresses and sizes");

    this->serializeInitSequenceCommands(recvStream,
                                        recvSOStream,
                                        dmaStream,
                                        cgComplex.cgInfo.cgIdx[(int)SchedulersIndex::recvScaleUp],
                                        cgComplex.cgInfo.cgBaseAddr,
                                        m_staticBufferAddressesAndSizes,
                                        device,
                                        apiId);

    LOG_HCL_TRACE(HCL_SCAL,
                  "serializeUpdateGlobalContextCommand on cg 0x{:x}, cg index: {}, longSO=0x{:x}",
                  (uint64_t)cgComplex.cgHandle,
                  cgComplex.cgInfo.cgIdx[(int)SchedulersIndex::recvScaleUp],
                  (uint64_t)cgComplex.cgInfo.longSoAddr);

    recvStream.submit();
    recvSOStream.submit();
    dmaStream.submit();

    // Wait for completion
    Gen2ArchScalManager::waitOnCg(cgComplex, 1);
    LOG_HCL_DEBUG(HCL_SCAL, "HCL initialized ScalManager Global Context");
}

void Gaudi2ScalManager::init()
{
    Gen2ArchScalManager::init();

    for (size_t i = 0; i < m_archStreams.size(); i++)
    {
        scal_comp_group_handle_t internalCgHandle = m_cgInfoArray[i][(int)SchedulerType::internal].cgHandle;
        scal_comp_group_handle_t externalCgHandle = m_cgInfoArray[i][(int)SchedulerType::external].cgHandle;
        m_archStreams[i]                          = std::unique_ptr<ArchStream>(
            new Gaudi2ArchStream(i, *m_scalWrapper, externalCgHandle, internalCgHandle, m_scalNames, m_commands));
    }
}