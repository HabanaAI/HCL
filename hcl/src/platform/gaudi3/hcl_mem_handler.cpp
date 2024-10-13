#include "platform/gaudi3/hcl_mem_handler.h"
#include "platform/gen2_arch_common/hcl_mem_handler.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"
#include "platform/gen2_arch_common/intermediate_buffer_container.h"

HclCollectiveMemHandlerGaudi3::HclCollectiveMemHandlerGaudi3(int                   archStreamId,
                                                             HclAddressGenerator&  addressGenerator,
                                                             DeviceBufferManager&  intermediateBufferManager,
                                                             HclCommandsGen2Arch&  commands,
                                                             HclGraphSyncGen2Arch& graphSync)
: HclCollectiveMemHandlerGen2Arch(archStreamId, addressGenerator, intermediateBufferManager, commands, graphSync)
{
}

void HclCollectiveMemHandlerGaudi3::generateBaseAddressOrSubBuffIdx(SliceState&       sliceState,
                                                                    unsigned int&     sliceIter,
                                                                    BoxNumInfo&       boxNumInfo,
                                                                    HCL_CollectiveOp& currentOp,
                                                                    uint64_t&         offset,
                                                                    uint64_t&         baseAddress,
                                                                    uint32_t&         subBuffIndex)
{
    baseAddress = m_addressGenerator.generateScaleUpRecvAddress(sliceState, sliceIter, boxNumInfo, currentOp, offset);
    LOG_HCL_TRACE(HCL, "Setting scale-up receive base address to 0x{:x}", baseAddress);
}
