#pragma once

#include "platform/gen2_arch_common/hcl_mem_handler.h"

class HclCollectiveMemHandlerGaudi3 : public HclCollectiveMemHandlerGen2Arch
{
public:
    HclCollectiveMemHandlerGaudi3(int                        archStreamId,
                                  HclAddressGenerator&       addressGenerator,
                                  DeviceSimbPoolManagerBase& deviceSimbPoolManager,
                                  HclCommandsGen2Arch&       commands,
                                  HclGraphSyncGen2Arch&      graphSync);

    virtual void generateBaseAddressOrSubBuffIdx(SliceState&       sliceState,
                                                 unsigned int&     sliceIter,
                                                 BoxNumInfo&       boxNumInfo,
                                                 HCL_CollectiveOp& currentOp,
                                                 uint64_t&         offset,
                                                 uint64_t&         baseAddress,
                                                 uint32_t&         subBuffIndex) override;
};
