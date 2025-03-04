#pragma once

#include <cstdint>  // for uint64_t
#include <functional>
#include "platform/gen2_arch_common/device_buffer_manager.h"  // for e_devicePoolID

class CommonState;

using IntermediateAddressGenerator =
    std::function<uint64_t(CommonState& commonState, e_devicePoolID poolIdx, unsigned bufferOffset)>;

class ExplicitAddressContainer
{
public:
    ExplicitAddressContainer() = default;
    ExplicitAddressContainer(IntermediateAddressGenerator generator);
    virtual ~ExplicitAddressContainer() = default;

    void setScaleupSendAddr(uint64_t addr) { m_explicitSuSendAddr = addr; }
    void setScaleupRecvAddr(uint64_t addr) { m_explicitSuRecvAddr = addr; }
    void setScaleupSendPool(e_devicePoolID pool) { m_explicitSuSendPool = pool; }
    void setScaleupRecvPool(e_devicePoolID pool) { m_explicitSuRecvPool = pool; }

    void setScaleoutSendAddr(uint64_t addr) { m_explicitSoSendAddr = addr; }
    void setScaleoutRecvAddr(uint64_t addr) { m_explicitSoRecvAddr = addr; }
    void setScaleoutSendPool(e_devicePoolID pool) { m_explicitSoSendPool = pool; }
    void setScaleoutRecvPool(e_devicePoolID pool) { m_explicitSoRecvPool = pool; }

    void setScaleupMemcpySrcAddr(uint64_t addr) { m_explicitSuMemcpySrcAddr = addr; }
    void setScaleupMemcpyDstAddr(uint64_t addr) { m_explicitSuMemcpyDstAddr = addr; }
    void setScaleupMemcpySrcPool(e_devicePoolID pool) { m_explicitSuMemcpySrcPool = pool; }
    void setScaleupMemcpyDstPool(e_devicePoolID pool) { m_explicitSuMemcpyDstPool = pool; }

    void setScaleoutMemcpySrcAddr(uint64_t addr) { m_explicitSoMemcpySrcAddr = addr; }
    void setScaleoutMemcpyDstAddr(uint64_t addr) { m_explicitSoMemcpyDstAddr = addr; }
    void setScaleoutMemcpySrcPool(e_devicePoolID pool) { m_explicitSoMemcpySrcPool = pool; }
    void setScaleoutMemcpyDstPool(e_devicePoolID pool) { m_explicitSoMemcpyDstPool = pool; }

    void setGdrMemcpySrcAddr(uint64_t addr) { m_explicitGdrMemcpySrcAddr = addr; }
    void setGdrMemcpyDstAddr(uint64_t addr) { m_explicitGdrMemcpyDstAddr = addr; }
    void setGdrMemcpySrcPool(e_devicePoolID pool) { m_explicitGdrMemcpySrcPool = pool; }
    void setGdrMemcpyDstPool(e_devicePoolID pool) { m_explicitGdrMemcpyDstPool = pool; }

    uint64_t consumeScaleUpSendAddress(CommonState& commonState);
    uint64_t consumeScaleUpRecvAddress(CommonState& commonState);
    uint64_t consumeSoSendAddress(CommonState& commonState);
    uint64_t consumeSoRecvAddress(CommonState& commonState);
    uint64_t consumeMemcpyDstAddr(CommonState& commonState, bool isForScaleout, bool isGDRMemcpy);
    uint64_t consumeMemcpySrcAddr(CommonState& commonState, bool isForScaleout, bool isGDRMemcpy);

private:
    uint64_t tryConsume(CommonState&    commonState,
                        uint64_t&       explicitAddr,
                        e_devicePoolID& explicitPool,
                        bool            calcIntermediateAddr = false);

    IntermediateAddressGenerator m_generateIntermediateAddress;

    uint64_t       m_explicitSuSendAddr = 0;
    uint64_t       m_explicitSuRecvAddr = 0;
    e_devicePoolID m_explicitSuSendPool = NO_POOL;
    e_devicePoolID m_explicitSuRecvPool = NO_POOL;

    uint64_t       m_explicitSoSendAddr = 0;
    uint64_t       m_explicitSoRecvAddr = 0;
    e_devicePoolID m_explicitSoSendPool = NO_POOL;
    e_devicePoolID m_explicitSoRecvPool = NO_POOL;

    uint64_t       m_explicitSuMemcpySrcAddr = 0;
    uint64_t       m_explicitSuMemcpyDstAddr = 0;
    e_devicePoolID m_explicitSuMemcpySrcPool = NO_POOL;
    e_devicePoolID m_explicitSuMemcpyDstPool = NO_POOL;

    uint64_t       m_explicitSoMemcpySrcAddr = 0;
    uint64_t       m_explicitSoMemcpyDstAddr = 0;
    e_devicePoolID m_explicitSoMemcpySrcPool = NO_POOL;
    e_devicePoolID m_explicitSoMemcpyDstPool = NO_POOL;

    uint64_t       m_explicitGdrMemcpySrcAddr = 0;
    uint64_t       m_explicitGdrMemcpyDstAddr = 0;
    e_devicePoolID m_explicitGdrMemcpySrcPool = NO_POOL;
    e_devicePoolID m_explicitGdrMemcpyDstPool = NO_POOL;
};