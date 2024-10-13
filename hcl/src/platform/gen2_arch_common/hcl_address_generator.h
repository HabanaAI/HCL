#pragma once

#include <cstdint>                                            // for uint64_t
#include "hcl_api_types.h"                                    // for HCL_CollectiveOp
#include "platform/gen2_arch_common/device_buffer_manager.h"  // for e_devicePoolID

class HclCommandsGen2Arch;
class CommonState;
class BoxNumInfo;

class HclAddressGenerator
{
public:
    HclAddressGenerator(HclCommandsGen2Arch& commands);
    virtual ~HclAddressGenerator() = default;

    uint64_t generateScaleUpRecvIndices(CommonState& commonState, uint32_t streamId);

    uint64_t generateScaleUpRecvAddress(CommonState&     commonState,
                                        unsigned         sliceIter,
                                        BoxNumInfo&      boxNumInfo,
                                        HCL_CollectiveOp currentOp,
                                        uint64_t         offset);

    uint64_t generateScaleUpSendAddress(CommonState&     commonState,
                                        unsigned         sliceIter,
                                        BoxNumInfo&      boxNumInfo,
                                        HCL_CollectiveOp currentOp,
                                        uint64_t         offset);

    uint64_t generateScaleOutSendAddress(CommonState&     commonState,
                                         unsigned         sliceIter,
                                         BoxNumInfo&      boxNumInfo,
                                         HCL_CollectiveOp currentOp,
                                         uint64_t         offset);

    uint64_t generateScaleOutRecvAddress(CommonState&     commonState,
                                         unsigned         sliceIter,
                                         BoxNumInfo&      boxNumInfo,
                                         HCL_CollectiveOp currentOp,
                                         uint64_t         offset);

    uint64_t generateMemcpySrcAddress(CommonState& commonState,
                                      unsigned     sliceIter,
                                      BoxNumInfo&  boxNumInfo,
                                      bool         reductionSignalToCg,
                                      uint32_t     dmaType,
                                      uint64_t     offset,
                                      bool         isReduction,
                                      bool         useSibo,
                                      bool         isForScaleOut,
                                      bool         isReductionStream = false,
                                      bool         isGDRMemcpy       = false);

    uint64_t generateMemcpyDstAddress(CommonState& commonState,
                                      unsigned     sliceIter,
                                      BoxNumInfo&  boxNumInfo,
                                      bool         reductionSignalToCg,
                                      uint32_t     dmaType,
                                      uint64_t     offset,
                                      bool         reductionIsFirstBoxMemcpy,
                                      bool         isReduction       = false,
                                      bool         useSibo           = false,
                                      bool         isForScaleout     = false,
                                      bool         isReductionStream = false,
                                      bool         isGDRMemcpy       = false);

    uint64_t
    generateIntermediateAddress(CommonState& commonState, bool isForScaleOut, bool useGDRPool, unsigned bufferOffset);

    uint64_t generateIntermediateAddress(CommonState& commonState, e_devicePoolID poolIdx, unsigned bufferOffset);

    virtual uint64_t recalcAddressForDisregardRank(HCL_CollectiveOp currentOp, uint64_t address, uint64_t offset) = 0;

private:
    HclCommandsGen2Arch& m_commands;
};
