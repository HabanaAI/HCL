#pragma once

#include <cstdint>                                            // for uint64_t
#include "hcl_api_types.h"                                    // for HCL_CollectiveOp
#include "platform/gen2_arch_common/device_buffer_manager.h"  // for e_devicePoolID
#include "explicit_addr_container.h"

class HclCommandsGen2Arch;
class CommonState;
struct SliceState;
class NonCollectiveState;
class BoxNumInfo;

class HclAddressGenerator
{
public:
    HclAddressGenerator();
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

    uint64_t generateScaleOutRecvAddress(SliceState&      sliceState,
                                         unsigned         sliceIter,
                                         BoxNumInfo&      boxNumInfo,
                                         HCL_CollectiveOp currentOp,
                                         uint64_t         offset);

    uint64_t generateScaleOutSendAddress(NonCollectiveState& commonState,
                                         unsigned            sliceIter,
                                         BoxNumInfo&         boxNumInfo,
                                         uint64_t            offset);

    uint64_t generateScaleOutRecvAddress(NonCollectiveState& commonState,
                                         unsigned            sliceIter,
                                         BoxNumInfo&         boxNumInfo,
                                         uint64_t            offset);

    uint64_t generateMemcpySrcAddress(CommonState& commonState,
                                      unsigned     sliceIter,
                                      BoxNumInfo&  boxNumInfo,
                                      uint64_t     offset,
                                      bool         isForScaleOut,
                                      bool         isGDRMemcpy        = false,
                                      bool         isForContReduction = false);

    uint64_t generateMemcpyDstAddress(CommonState& commonState,
                                      unsigned     sliceIter,
                                      BoxNumInfo&  boxNumInfo,
                                      uint64_t     offset,
                                      bool         isForScaleout      = false,
                                      bool         isGDRMemcpy        = false,
                                      bool         isForContReduction = false);

    uint64_t
    generateIntermediateAddress(CommonState& commonState, bool isForScaleOut, bool useGDRPool, unsigned bufferOffset);

    uint64_t generateIntermediateAddress(CommonState& commonState, e_devicePoolID poolIdx, unsigned bufferOffset);

    ExplicitAddressContainer& addressContainer() { return m_addrContainer; }

    virtual uint64_t recalcAddressForDisregardRank(HCL_CollectiveOp currentOp, uint64_t address, uint64_t offset) = 0;

private:
    ExplicitAddressContainer m_addrContainer;
};
