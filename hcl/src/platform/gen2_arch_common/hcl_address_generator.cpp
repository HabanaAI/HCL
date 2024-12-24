#include "hcl_address_generator.h"

#include <cstdint>                                            // for uint64_t
#include "device_buffer_manager.h"                            // for BUFFER POOLS
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclCommandsGen2Arch
#include "hcl_dynamic_communicator.h"                         // for HclDyna...
#include "platform/gen2_arch_common/collective_states.h"
#include "hcl_api_types.h"
#include "platform/gen2_arch_common/device_buffer_manager.h"
#include "hcl_math_utils.h"

HclAddressGenerator::HclAddressGenerator() {}

uint64_t HclAddressGenerator::generateScaleUpRecvIndices(CommonState& commonState, uint32_t streamId)
{
    return commonState.m_intermediateBufferManager.getSliceId(SCALEUP_AND_ALL2ALL_POOL,
                                                              streamId);  // Accu buffer
}

uint64_t HclAddressGenerator::generateScaleUpRecvAddress(CommonState&     commonState,
                                                         unsigned         sliceIter,
                                                         BoxNumInfo&      boxNumInfo,
                                                         HCL_CollectiveOp currentOp,
                                                         uint64_t         offset)
{
    if (m_explicitSuRecvAddr > 0) return m_explicitSuRecvAddr;

    uint64_t currentBoxRecvAddress = commonState.getRecvAddress(sliceIter) + boxNumInfo.m_boxNum *
                                                                                 commonState.m_boxStrideCount *
                                                                                 commonState.m_dataTypeSizeInBytes;

    uint64_t addr = 0;
    switch (currentOp)
    {
        case eHCLReduceScatter:
            addr = commonState.getIntermediateBuffer(SCALEUP_AND_ALL2ALL_POOL);
            break;
        case eHCLGather:
        case eHCLAllGather:
            addr = currentBoxRecvAddress;
            break;
        case eHCLAll2All:
            if (boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyScaleupGroup())
            {
                addr = currentBoxRecvAddress;
            }
            else
            {
                addr = commonState.getIntermediateBuffer(SCALEUP_AND_ALL2ALL_POOL);
            }
            break;
        case eHCLScatter:
            // the qps in gaudi3 are preconfigured in a way that is unsuitable for the scatter collective routine since
            // the offsets are wrong. in order to overcome this issue we play on the fact that each receiver needs to
            // configure only the offsets for the nics that are connected to the sender. all these nics require the same
            // offset from the beginning of the buffer. we can use disregard rank and calculate the addresses
            // specifically for those nics.
            addr = recalcAddressForDisregardRank(currentOp, commonState.getRecvAddress(sliceIter), offset);
            break;
        case eHCLSimpleBroadcast:
            addr = commonState.getRecvAddress(sliceIter);
            break;

        case eHCLReduce:
        case eHCLAllReduce:
        case eHCLSinglePeerBroadcast:
        case eHCLBroadcast:
        case eHCLNoCollective:
        case eHCLCollectiveLastValue:
            VERIFY(false, "wrong current op {} at generateScaleUpRecvAddress", currentOp);
    }

    return addr;
}

uint64_t HclAddressGenerator::generateScaleUpSendAddress(CommonState&     commonState,
                                                         unsigned         sliceIter,
                                                         BoxNumInfo&      boxNumInfo,
                                                         HCL_CollectiveOp currentOp,
                                                         uint64_t         offset)
{
    if (m_explicitSuSendAddr > 0) return m_explicitSuSendAddr;
    uint64_t currentBoxSendAddress = commonState.getSendAddress(sliceIter) + boxNumInfo.m_boxNum *
                                                                                 commonState.m_boxStrideCount *
                                                                                 commonState.m_dataTypeSizeInBytes;
    uint64_t currentBoxRecvAddress = commonState.getRecvAddress(sliceIter) + boxNumInfo.m_boxNum *
                                                                                 commonState.m_boxStrideCount *
                                                                                 commonState.m_dataTypeSizeInBytes;

    uint64_t addr = 0;
    switch (currentOp)
    {
        case eHCLReduceScatter:
        case eHCLAll2All:
            addr = currentBoxSendAddress;
            break;
        case eHCLAllGather:
            if (commonState.m_collectiveOp == eHCLAllGather)
            {
                if (boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyScaleupGroup())
                {
                    addr = commonState.getSendAddress(sliceIter);
                }
                else
                {
                    addr = currentBoxRecvAddress + offset;
                }
            }
            else if (commonState.m_collectiveOp == eHCLBroadcast)
            {
                if (commonState.isRoot())
                {
                    addr = commonState.getSendAddress(sliceIter);
                }
                else
                {
                    addr = currentBoxRecvAddress;
                }
            }
            else
            // AR, Broadcast & single peer broadcast -> data from RS/scatter is already in output buffer
            // for Broadcast & single peer broadcast,
            // box stride is 0 so currentBoxRecvAddress degenerates to Recv buffer address
            {
                addr = currentBoxRecvAddress;
            }
            break;
        case eHCLGather:
            if (boxNumInfo.m_boxNum != commonState.m_dynamicComm.getMyScaleupGroup())
            {
                addr = commonState.getIntermediateBuffer(REDUCE_POOL);
            }
            else if (commonState.m_collectiveOp == eHCLGather)
            {
                addr = commonState.getSendAddress(sliceIter);
            }
            else if (!commonState.m_isMultiScaleupGroup)
            {
                addr = commonState.getIntermediateBuffer(SCALEUP_AND_ALL2ALL_POOL);
            }
            else if (commonState.m_16BitReduction)
            {
                addr = commonState.getIntermediateBuffer(REDUCE_POOL);
            }
            else
            {
                addr = commonState.getIntermediateBuffer(SCALEOUT_POOL);
            }
            break;
        case eHCLScatter:
        case eHCLSimpleBroadcast:
            if (commonState.isRoot())
            {
                addr = commonState.getSendAddress(sliceIter);
            }
            else  // single peer broadcast: root peers scatter within their box from output buffer
            {
                addr = commonState.getRecvAddress(sliceIter);
            }
            break;

        case eHCLReduce:
        case eHCLAllReduce:
        case eHCLSinglePeerBroadcast:
        case eHCLBroadcast:
        case eHCLNoCollective:
        case eHCLCollectiveLastValue:
            VERIFY(false, "wrong current op {} at generateScaleUpSendAddress", currentOp);
    }

    return addr;
}

uint64_t HclAddressGenerator::getAndReset_explicitSoSendAddress(CommonState& commonState)
{
    uint64_t addr = 0;
    if (m_explicitSoSendAddr > 0)
    {
        addr                 = m_explicitSoSendAddr;
        m_explicitSoSendAddr = 0;
    }
    return addr;
}

uint64_t HclAddressGenerator::generateScaleOutSendAddress(CommonState&     commonState,
                                                          unsigned         sliceIter,
                                                          BoxNumInfo&      boxNumInfo,
                                                          HCL_CollectiveOp currentOp,
                                                          uint64_t         offset)
{
    VERIFY(boxNumInfo.m_orientation == BoxNumInfo::boxOrientation::NEXT_BOX);
    if (uint64_t explicit_addr = getAndReset_explicitSoSendAddress(commonState)) return explicit_addr;

    uint64_t currentBoxSendAddress = commonState.getSendAddress(sliceIter) + boxNumInfo.m_boxNum *
                                                                                 commonState.m_boxStrideCount *
                                                                                 commonState.m_dataTypeSizeInBytes;
    uint64_t myBoxRecvAddress = commonState.getRecvAddress(sliceIter) + commonState.m_dynamicComm.getMyScaleupGroup() *
                                                                            commonState.m_boxStrideCount *
                                                                            commonState.m_dataTypeSizeInBytes;

    uint64_t addr = 0;

    switch (currentOp)
    {
        case eHCLReduceScatter:
        case eHCLAll2All:
            if (commonState.m_dynamicComm.getScaleupGroupSize() != 1)
            {
                addr = commonState.getIntermediateBuffer(SCALEUP_AND_ALL2ALL_POOL);
            }
            else  // peers only
            {
                addr = currentBoxSendAddress + offset;
            }
            break;
        case eHCLAllGather:
            if (commonState.m_collectiveOp == eHCLAllGather)
            {
                addr = commonState.getSendAddress(sliceIter);
            }
            else
            {
                addr = myBoxRecvAddress + offset;
            }
            break;
        case eHCLGather:
            if (commonState.m_collectiveOp == eHCLGather)
            {
                addr = commonState.getSendAddress(sliceIter);
            }
            else if (commonState.m_16BitReduction)
            {
                addr = commonState.getIntermediateBuffer(REDUCE_POOL);
            }
            else
            {
                addr = commonState.getIntermediateBuffer(SCALEOUT_POOL);
            }
            break;
        case eHCLScatter:
            if (commonState.isRoot())
            {
                addr = commonState.getSendAddress(sliceIter);
            }
            else
            {
                addr = commonState.getRecvAddress(sliceIter);
            }
            if (commonState.m_collectiveOp != eHCLSinglePeerBroadcast)
            {
                addr += offset;
            }
            break;
        case eHCLSimpleBroadcast:
            addr = commonState.getSendAddress(sliceIter);
            break;
        case eHCLNoCollective:
            addr = currentBoxSendAddress + offset;
            break;

        case eHCLReduce:
        case eHCLAllReduce:
        case eHCLSinglePeerBroadcast:
        case eHCLBroadcast:
        case eHCLCollectiveLastValue:
            VERIFY(false, "wrong current op {} at generateScaleOutSendAddress", currentOp);
    }

    return addr;
}

uint64_t HclAddressGenerator::getAndReset_explicitSoRecvAddress(CommonState& commonState)
{
    uint64_t addr = 0;
    if (m_explicitSoRecvAddr > 0)
    {
        addr                 = m_explicitSoRecvAddr;
        m_explicitSoRecvAddr = 0;
    }
    return addr;
}

uint64_t HclAddressGenerator::generateScaleOutRecvAddress(CommonState&     commonState,
                                                          unsigned         sliceIter,
                                                          BoxNumInfo&      boxNumInfo,
                                                          HCL_CollectiveOp currentOp,
                                                          uint64_t         offset)
{
    if (uint64_t explicit_address = getAndReset_explicitSoRecvAddress(commonState)) return explicit_address;

    uint64_t currentBoxRecvAddress = commonState.getRecvAddress(sliceIter) + boxNumInfo.m_boxNum *
                                                                                 commonState.m_boxStrideCount *
                                                                                 commonState.m_dataTypeSizeInBytes;
    uint64_t addr = 0;

    switch (currentOp)
    {
        case eHCLReduceScatter:
            if (commonState.m_isGdr)
            {
                addr = generateIntermediateAddress(commonState, SCALEOUT_GDR_POOL, 0);
            }
            else
            {
                addr = generateIntermediateAddress(
                    commonState,
                    SCALEOUT_POOL,
                    mod(commonState.calcBoxIterRecv(boxNumInfo), commonState.m_scaleoutBuffersAmount));
            }
            break;
        case eHCLAllGather:
            addr = currentBoxRecvAddress + offset;
            break;
        case eHCLAll2All:
            addr = currentBoxRecvAddress;
            break;
        case eHCLGather:
            if (commonState.isRoot())
            {
                addr = currentBoxRecvAddress + offset;
            }
            else
            {
                addr = commonState.getIntermediateBuffer(REDUCE_POOL);
            }
            break;
        case eHCLScatter:
            addr = commonState.getRecvAddress(sliceIter);
            if (commonState.m_collectiveOp != eHCLSinglePeerBroadcast)
            {
                addr += offset;
            }
            break;
        case eHCLSimpleBroadcast:
            addr = commonState.getRecvAddress(sliceIter);
            break;
        case eHCLNoCollective:
            addr = currentBoxRecvAddress + offset;
            break;

        case eHCLReduce:
        case eHCLAllReduce:
        case eHCLSinglePeerBroadcast:
        case eHCLBroadcast:
        case eHCLCollectiveLastValue:
            VERIFY(false, "wrong current op {} at generateScaleOutRecvAddress", currentOp);
    }

    return addr;
}

uint64_t HclAddressGenerator::generateMemcpySrcAddress(CommonState& commonState,
                                                       unsigned     sliceIter,
                                                       BoxNumInfo&  boxNumInfo,
                                                       bool         reductionSignalToCg,
                                                       uint64_t     offset,
                                                       bool         isReduction,
                                                       bool         useSibo,
                                                       bool         isForScaleOut,
                                                       bool         isReductionStream,
                                                       bool         isGDRMemcpy)
{
    if (isForScaleOut)
    {
        if (m_explicitSoMemcpySrcAddr > 0) return m_explicitSoMemcpySrcAddr;
    }
    else
    {
        if (m_explicitSuMemcpySrcAddr > 0) return m_explicitSuMemcpySrcAddr;
    }

    if (isGDRMemcpy)
    {
        return generateIntermediateAddress(commonState, isForScaleOut, isGDRMemcpy, 0);
    }

    uint64_t currentBoxSendAddress = commonState.getSendAddress(sliceIter) + boxNumInfo.m_boxNum *
                                                                                 commonState.m_boxStrideCount *
                                                                                 commonState.m_dataTypeSizeInBytes;

    uint64_t addr = 0;

    switch (commonState.m_currentOp)
    {
        case eHCLReduceScatter:
            if (isForScaleOut)
            {
                addr = commonState.getIntermediateBuffer(SCALEOUT_POOL);
            }
            else
            {
                addr = currentBoxSendAddress + offset;
            }
            break;
        case eHCLAllGather:
        case eHCLGather:
        case eHCLSimpleBroadcast:
            addr = commonState.getSendAddress(sliceIter);
            break;
        case eHCLAll2All:
            addr = currentBoxSendAddress + offset;
            break;
        case eHCLScatter:
            addr = currentBoxSendAddress;
            if (commonState.m_collectiveOp != eHCLSinglePeerBroadcast)
            {
                addr += offset;
            }
            break;
        case eHCLReduce:
        case eHCLAllReduce:
        case eHCLSinglePeerBroadcast:
        case eHCLBroadcast:
        case eHCLNoCollective:
        case eHCLCollectiveLastValue:
            VERIFY(false, "wrong current op {} at generateMemcpySrcAddress", commonState.m_currentOp);
    }

    return addr;
}

uint64_t HclAddressGenerator::generateMemcpyDstAddress(CommonState& commonState,
                                                       unsigned     sliceIter,
                                                       BoxNumInfo&  boxNumInfo,
                                                       bool         reductionSignalToCg,
                                                       uint64_t     offset,
                                                       bool         reductionIsFirstBoxMemcpy,
                                                       bool         isReduction,
                                                       bool         useSibo,
                                                       bool         isForScaleout,
                                                       bool         isReductionStream,
                                                       bool         isGDRMemcpy)
{
    if (!isForScaleout)
    {
        if (m_explicitSuMemcpyDstAddr > 0) return m_explicitSuMemcpyDstAddr;
    }
    else
    {
        if (m_explicitSoMemcpyDstAddr > 0) return m_explicitSoMemcpyDstAddr;
    }

    if (isGDRMemcpy)
    {
        unsigned bufferOffset = 0;
        if (isGDRMemcpy)
        {
            bufferOffset = mod(commonState.calcBoxIterRecv(boxNumInfo), commonState.m_scaleoutBuffersAmount);
        }
        else
        {
            bufferOffset = useSibo ? 0 : commonState.m_dynamicComm.getRankInScaleupGroup();
        }
        return generateIntermediateAddress(commonState, isForScaleout, false, bufferOffset);
    }

    uint64_t currentBoxRecvAddress = commonState.getRecvAddress(sliceIter) + boxNumInfo.m_boxNum *
                                                                                 commonState.m_boxStrideCount *
                                                                                 commonState.m_dataTypeSizeInBytes;

    uint64_t myBoxRecvAddress = commonState.getRecvAddress(sliceIter) + commonState.m_dynamicComm.getMyScaleupGroup() *
                                                                            commonState.m_boxStrideCount *
                                                                            commonState.m_dataTypeSizeInBytes;

    uint64_t addr = 0;

    switch (commonState.m_currentOp)
    {
        case eHCLReduceScatter:
            if (!commonState.m_isMultiScaleupGroup)
            {
                if (commonState.m_collectiveOp == eHCLReduce && !commonState.isRoot())
                {
                    addr = commonState.getIntermediateBuffer(SCALEUP_AND_ALL2ALL_POOL);
                }
                else
                {
                    addr = currentBoxRecvAddress;
                    if (commonState.m_collectiveOp != eHCLReduceScatter)
                    {
                        addr += offset;
                    }
                }
            }
            // multiScaleupGroup
            else if (!isForScaleout)
            {
                if (boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyScaleupGroup())
                {
                    addr = commonState.getIntermediateBuffer(SCALEOUT_POOL);
                }
                else
                {
                    addr = commonState.getIntermediateBuffer(SCALEUP_AND_ALL2ALL_POOL);
                }
            }
            else  // scaleout
            {
                if (commonState.m_collectiveOp == eHCLReduce && !commonState.isRoot())
                {
                    if (commonState.m_16BitReduction)
                    {
                        addr = commonState.getIntermediateBuffer(REDUCE_POOL);
                    }
                    else
                    {
                        addr = commonState.getIntermediateBuffer(SCALEOUT_POOL);
                    }
                }
                else if (commonState.m_collectiveOp == eHCLReduceScatter)
                {
                    addr = commonState.getRecvAddress(sliceIter);
                }
                else  // AR or Reduce root
                {
                    addr = myBoxRecvAddress + offset;
                }
            }
            break;
        case eHCLGather:
        case eHCLAllGather:
            addr = currentBoxRecvAddress + offset;
            break;
        case eHCLAll2All:
            if (boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyScaleupGroup())
            {
                addr = currentBoxRecvAddress + offset;
            }
            else
            {
                addr = commonState.getIntermediateBuffer(SCALEUP_AND_ALL2ALL_POOL) + offset;
            }
            break;
        case eHCLScatter:
            addr = commonState.getRecvAddress(sliceIter);
            if (commonState.m_collectiveOp != eHCLSinglePeerBroadcast)
            {
                addr += offset;
            }
            break;
        case eHCLSimpleBroadcast:
            addr = commonState.getRecvAddress(sliceIter);
            break;

        case eHCLReduce:
        case eHCLAllReduce:
        case eHCLSinglePeerBroadcast:
        case eHCLBroadcast:
        case eHCLNoCollective:
        case eHCLCollectiveLastValue:
            VERIFY(false, "wrong current op {} at generateMemcpyDstAddress", commonState.m_currentOp);
    }

    return addr;
}

uint64_t HclAddressGenerator::generateIntermediateAddress(CommonState& commonState,
                                                          bool         isForScaleOut,
                                                          bool         useGDRPool,
                                                          unsigned     bufferOffset)
{
    e_devicePoolID soPoolID = useGDRPool ? SCALEOUT_GDR_POOL : SCALEOUT_POOL;
    return generateIntermediateAddress(commonState, isForScaleOut ? soPoolID : SCALEUP_AND_ALL2ALL_POOL, bufferOffset);
}

uint64_t HclAddressGenerator::generateIntermediateAddress(CommonState&   commonState,
                                                          e_devicePoolID poolIdx,
                                                          unsigned       bufferOffset)
{
    // Use stream 0 anyway, as the offset to the current stream will be added with the base
    unsigned indexOfSubBuffer = commonState.m_intermediateBufferManager.getSliceId(poolIdx, 0) + bufferOffset;
    uint64_t intermediateBufferBaseAddress = commonState.m_intermediateBufferManager.getBufferBaseAddr(poolIdx);
    uint64_t sizeOfSlice                   = commonState.m_intermediateBufferManager.getSingleBufferSize(poolIdx);

    // BASE_ADDRESS + SLICE * INDEX + SLICE*MY_RANK
    uint64_t calculatedAddress = intermediateBufferBaseAddress + sizeOfSlice * indexOfSubBuffer;

    return calculatedAddress;
}
