#pragma once

#include <cstddef>                                            // for NULL
#include <cstdint>                                            // for set
#include "infra/scal/gen2_arch_common/scal_names.h"           // for SchedulersIndex
#include "platform/gen2_arch_common/hcl_address_generator.h"  // for HclAddressGenerator
#include "platform/gen2_arch_common/types.h"                  // for GEN2ARCH_HLS_BOX_SIZE
#include "platform/gen2_arch_common/signals/types.h"          // for WaitEvent
#include "platform/gen2_arch_common/signals/manager.h"        // for SignalsManager
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
#include "platform/gen2_arch_common/collective_states.h"

class HclGraphSyncGen2Arch;
class DeviceBufferManager;
class IntermediateBufferContainer;

namespace hcl
{
class ScalStream;
}  // namespace hcl

enum DmaType
{
    DMA_TYPE_CAST_UP   = 0,
    DMA_TYPE_CAST_DOWN = 1,
    DMA_TYPE_MEMCPY    = 2
};

class HclCollectiveMemHandlerGen2Arch
{
public:
    HclCollectiveMemHandlerGen2Arch(int                   archStreamId,
                                    HclAddressGenerator&  addressGenerator,
                                    DeviceBufferManager&  intermediateBufferManager,
                                    HclCommandsGen2Arch&  commands,
                                    HclGraphSyncGen2Arch& graphSync);

    virtual ~HclCollectiveMemHandlerGen2Arch();

    DeviceBufferManager& getIntermediateBufferManager() { return m_intermediateBufferManager; }

    void createMemCopyCommands(SliceState&      sliceState,
                               SignalsManager*  signalsManager,
                               unsigned         sliceIter,
                               BoxNumInfo&      boxNumInfo,
                               uint64_t         chunkCount,
                               hcl::ScalStream& scalStream,
                               uint32_t         dmaType,
                               bool             isForScaleout,
                               e_devicePoolID   poolIdx            = NO_POOL,
                               bool             isGDRMemcpy        = false,
                               bool             isForContReduction = false);

    SignalEvent chooseMemCopyEvent(CommonState& commonState,
                                   uint32_t     dmaType,
                                   bool         isFirstBox,
                                   bool         isGDRMemcpy,
                                   bool         useSibo,
                                   bool         isForScaleout,
                                   bool         isForContReduction = false);

    void createMemCopyCommandsNonCollective(hcl::ScalStream& scalStream,
                                            HCL_Rank         myRank,
                                            uint64_t         chunkCount,
                                            hcclDataType_t   dataType,
                                            uint64_t         recvBaseAddress,
                                            uint64_t         sendBaseAddress,
                                            uint64_t         podSize,
                                            uint8_t          apiId);

    void signalToSoViaEmptyDmaCommand(uint32_t soAddress, hcl::ScalStream& scalStream, CommonState& commonState);

    virtual void memsetIMBs([[maybe_unused]] IntermediateBufferContainer* imbContainer,
                            [[maybe_unused]] SignalsManager*              signalsManager,
                            [[maybe_unused]] SliceState&                  sendSliceState,
                            [[maybe_unused]] SliceState&                  recvSliceState,
                            [[maybe_unused]] unsigned int                 sizeInBytes,
                            [[maybe_unused]] hcl::syncInfo                longSo,
                            [[maybe_unused]] unsigned                     schedIdx,
                            [[maybe_unused]] hcl::ScalStream&             garbageCollectionStream,
                            [[maybe_unused]] HCL_StreamId                 m_streamId,
                            [[maybe_unused]] e_devicePoolID               poolId,
                            [[maybe_unused]] uint8_t                      streamCtxtID,
                            [[maybe_unused]] hcclDataType_t               dataType) {};

    virtual void enqueueInternalCompletionMemsetSignals([[maybe_unused]] SignalsManager* signalsManager,
                                                        [[maybe_unused]] e_devicePoolID  poolId) {};

    virtual void generateBaseAddressOrSubBuffIdx(SliceState&       sliceState,
                                                 unsigned int&     sliceIter,
                                                 BoxNumInfo&       boxNumInfo,
                                                 HCL_CollectiveOp& currentOp,
                                                 uint64_t&         offset,
                                                 uint64_t&         baseAddress,
                                                 uint32_t&         subBuffIndex) = 0;

protected:
    int                   m_archStreamId;
    HclAddressGenerator&  m_addressGenerator;
    DeviceBufferManager&  m_intermediateBufferManager;
    HclCommandsGen2Arch&  m_commands;
    HclGraphSyncGen2Arch& m_graphSync;
    bool                  m_profilerDebugMode;
};
