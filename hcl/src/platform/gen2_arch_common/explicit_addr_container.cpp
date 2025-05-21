#include "explicit_addr_container.h"
#include "platform/gen2_arch_common/collective_states.h"
#include "hcl_math_utils.h"

ExplicitAddressContainer::ExplicitAddressContainer(IntermediateAddressGenerator generator)
: m_generateIntermediateAddress(generator)
{
}

uint64_t ExplicitAddressContainer::consumeScaleUpSendAddress(CommonState& commonState)
{
    uint64_t addr = tryConsume(commonState, m_explicitSuSendAddr, m_explicitSuSendPool);
    return addr;
}

uint64_t ExplicitAddressContainer::consumeScaleUpRecvAddress(CommonState& commonState)
{
    uint64_t addr = tryConsume(commonState, m_explicitSuRecvAddr, m_explicitSuRecvPool);
    return addr;
}

uint64_t ExplicitAddressContainer::consumeSoSendAddress(CommonState& commonState)
{
    uint64_t addr = tryConsume(commonState, m_explicitSoSendAddr, m_explicitSoSendPool);
    return addr;
}

uint64_t ExplicitAddressContainer::consumeSoRecvAddress(CommonState& commonState)
{
    uint64_t addr =
        tryConsume(commonState, m_explicitSoRecvAddr, m_explicitSoRecvPool, m_explicitSoRecvPool == SCALEOUT_POOL);
    return addr;
}

uint64_t ExplicitAddressContainer::consumeMemcpyDstAddr(CommonState& commonState, bool isForScaleout, bool isGDRMemcpy)
{
    uint64_t addr = 0;
    if (!isForScaleout)
    {
        addr = tryConsume(commonState, m_explicitSuMemcpyDstAddr, m_explicitSuMemcpyDstPool);
    }
    else
    {
        addr = tryConsume(commonState,
                          isGDRMemcpy ? m_explicitGdrMemcpyDstAddr : m_explicitSoMemcpyDstAddr,
                          isGDRMemcpy ? m_explicitGdrMemcpyDstPool : m_explicitSoMemcpyDstPool,
                          isGDRMemcpy && m_explicitSoRecvPool == SCALEOUT_POOL);
    }
    return addr;
}

uint64_t ExplicitAddressContainer::consumeMemcpySrcAddr(CommonState& commonState, bool isForScaleout, bool isGDRMemcpy)
{
    uint64_t addr = 0;
    if (!isForScaleout)
    {
        addr = tryConsume(commonState, m_explicitSuMemcpySrcAddr, m_explicitSuMemcpySrcPool);
    }
    else
    {
        addr = tryConsume(commonState,
                          isGDRMemcpy ? m_explicitGdrMemcpySrcAddr : m_explicitSoMemcpySrcAddr,
                          isGDRMemcpy ? m_explicitGdrMemcpySrcPool : m_explicitSoMemcpySrcPool);
    }
    return addr;
}

uint64_t ExplicitAddressContainer::tryConsume(CommonState&    commonState,
                                              uint64_t&       explicitAddr,
                                              e_devicePoolID& explicitPool,
                                              bool            calcIntermediateAddr)
{
    uint64_t addr = 0;
    VERIFY(!(explicitAddr > 0 && explicitPool != NO_POOL),
           "must exist explicitAddr({}) or explicitPool ({}) when consuming",
           explicitAddr,
           explicitPool);
    if (explicitAddr > 0)
    {
        addr         = explicitAddr;
        explicitAddr = 0;
    }
    else if (explicitPool != NO_POOL)
    {
        addr         = calcIntermediateAddr
                           ? m_generateIntermediateAddress(
                         commonState,
                         explicitPool,
                         mod(commonState.m_boxIter, DeviceSimbPoolManagerBase::getFactor(explicitPool)))
                           : commonState.getIntermediateBuffer(explicitPool);
        explicitPool = NO_POOL;
    }
    return addr;
}