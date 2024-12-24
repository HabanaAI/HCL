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

    uint64_t generateScaleOutRecvAddress(CommonState&     commonState,
                                         unsigned         sliceIter,
                                         BoxNumInfo&      boxNumInfo,
                                         HCL_CollectiveOp currentOp,
                                         uint64_t         offset);

    uint64_t generateMemcpySrcAddress(CommonState& commonState,
                                      unsigned     sliceIter,
                                      BoxNumInfo&  boxNumInfo,
                                      bool         reductionSignalToCg,
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

    void setScaleupSendAddr(uint64_t addr) { m_explicitSuSendAddr = addr; }
    void setScaleupRecvAddr(uint64_t addr) { m_explicitSuRecvAddr = addr; }

    void setScaleoutSendAddr(uint64_t addr) { m_explicitSoSendAddr = addr; }
    void setScaleoutRecvAddr(uint64_t addr) { m_explicitSoRecvAddr = addr; }

    void setScaleupMemcpySrcAddr(uint64_t addr) { m_explicitSuMemcpySrcAddr = addr; }
    void setScaleupMemcpyDstAddr(uint64_t addr) { m_explicitSuMemcpyDstAddr = addr; }

    void setScaleoutMemcpySrcAddr(uint64_t addr) { m_explicitSoMemcpySrcAddr = addr; }
    void setScaleoutMemcpyDstAddr(uint64_t addr) { m_explicitSoMemcpyDstAddr = addr; }

    uint64_t getAndReset_explicitSoSendAddress(CommonState& commonState);
    uint64_t getAndReset_explicitSoRecvAddress(CommonState& commonState);

private:
    uint64_t m_explicitSuSendAddr = 0;
    uint64_t m_explicitSuRecvAddr = 0;

    uint64_t m_explicitSoSendAddr = 0;
    uint64_t m_explicitSoRecvAddr = 0;

    uint64_t m_explicitSuMemcpySrcAddr = 0;
    uint64_t m_explicitSuMemcpyDstAddr = 0;

    uint64_t m_explicitSoMemcpySrcAddr = 0;
    uint64_t m_explicitSoMemcpyDstAddr = 0;
};
