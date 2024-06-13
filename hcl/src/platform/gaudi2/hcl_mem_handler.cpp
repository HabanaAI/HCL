#include "platform/gaudi2/hcl_mem_handler.h"
#include "platform/gen2_arch_common/hcl_mem_handler.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"

HclCollectiveMemHandlerGaudi2::HclCollectiveMemHandlerGaudi2(int                   archStreamId,
                                                             HclAddressGenerator&  addressGenerator,
                                                             DeviceBufferManager&  intermediateBufferManager,
                                                             HclCommandsGen2Arch&  commands,
                                                             HclGraphSyncGen2Arch& graphSync)
: HclCollectiveMemHandlerGen2Arch(archStreamId, addressGenerator, intermediateBufferManager, commands, graphSync)
{
}

void HclCollectiveMemHandlerGaudi2::generateBaseAddressOrRRIdx(SliceState&       sliceState,
                                                               unsigned int&     sliceIter,
                                                               BoxNumInfo&       boxNumInfo,
                                                               HCL_CollectiveOp& currentOp,
                                                               uint64_t&         offset,
                                                               uint64_t&         baseAddress,
                                                               uint32_t&         rrIndex)
{
    if (!sliceState.m_isReductionCollective || currentOp == eHCLAllGather || currentOp == eHCLGather)
    {
        baseAddress =
            m_addressGenerator.generateScaleUpRecvAddress(sliceState, sliceIter, boxNumInfo, currentOp, offset);
        LOG_HCL_TRACE(HCL, "Setting scale-up receive base address to 0x{:x}", baseAddress);
    }
    else
    {
        // Current RR implementation work in granularity of 8
        rrIndex =
            m_addressGenerator.generateScaleUpRecvIndices(sliceState, m_archStreamId) / RR_BUFFER_GRANULARITY_SCALEUP;
        LOG_HCL_TRACE(HCL, "Setting scale-up receive index to {}", rrIndex);
    }
}