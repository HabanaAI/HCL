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

    virtual void generateBaseAddressOrSubBuffIdx(SliceState&       sliceState,
                                                 unsigned int&     sliceIter,
                                                 BoxNumInfo&       boxNumInfo,
                                                 HCL_CollectiveOp& currentOp,
                                                 uint64_t&         offset,
                                                 uint64_t&         baseAddress,
                                                 uint32_t&         subBuffIndex) override;

    virtual void memsetIMBs(IntermediateBufferContainer* imbContainer,
                            SignalsManager*              signalsManager,
                            SliceState&                  sendSliceState,
                            SliceState&                  recvSliceState,
                            unsigned int                 sizeInBytes,
                            hcl::syncInfo                longSo,
                            unsigned                     schedIdx,
                            hcl::ScalStream&             garbageCollectionStream,
                            HCL_StreamId                 m_streamId,
                            e_devicePoolID               poolId,
                            uint8_t                      streamCtxtID,
                            hcclDataType_t               dataType) override;

    void enqueueInternalCompletionMemsetSignals(SignalsManager* signalsManager, e_devicePoolID poolId);
};
