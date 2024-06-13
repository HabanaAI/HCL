#pragma once

#include "platform/gen2_arch_common/hcl_mem_handler.h"

class HclCollectiveMemHandlerGaudi2 : public HclCollectiveMemHandlerGen2Arch
{
public:
    HclCollectiveMemHandlerGaudi2(int                   archStreamId,
                                  HclAddressGenerator&  addressGenerator,
                                  DeviceBufferManager&  intermediateBufferManager,
                                  HclCommandsGen2Arch&  commands,
                                  HclGraphSyncGen2Arch& graphSync);

    virtual void generateBaseAddressOrRRIdx(SliceState&       sliceState,
                                            unsigned int&     sliceIter,
                                            BoxNumInfo&       boxNumInfo,
                                            HCL_CollectiveOp& currentOp,
                                            uint64_t&         offset,
                                            uint64_t&         baseAddress,
                                            uint32_t&         rrIndex) override;
};
