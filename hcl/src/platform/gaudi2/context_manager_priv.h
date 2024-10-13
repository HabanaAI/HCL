#pragma once

#include <array>
#include <cstdint>
#include <set>
#include "hcl_types.h"
#include "platform/gaudi2/types.h"
#include "platform/gaudi2/communicator_descriptor.h"
#include "infra/scal/gen2_arch_common/scal_stream.h"
#include "platform/gaudi2/nic_passthrough_handler.h"
#include "hccl_types.h"                                              // for hcclRedOp_t
#include "platform/gen2_arch_common/nic_passthrough_handler_base.h"  // for DwordsNicsArray

class ContextManager;
class Gen2ArchServerConnectivity;

class UniqueCollectiveContext
{
public:
    UniqueCollectiveContext();

    uint32_t remote_index;
    bool     connection_enabled;
};

class RequiredCollectiveContext
{
public:
    RequiredCollectiveContext();

    RequiredCollectiveContext(HCL_CollectiveOp collectiveOp,
                              hcclRedOp_t      reduceOp,
                              uint32_t         soAddress,
                              bool             isSend,
                              uint32_t         addressMSB,
                              hcclDataType_t   dataType,
                              uint64_t         strideCount);

    virtual ~RequiredCollectiveContext() = default;

    void dwordDiff(const RequiredCollectiveContext& other, edwords_t& dwordsForUpdate);

    g2fw::nic_coll_ctxt_dword_t      m_remoteDescriptor;
    g2_nic_engine_reduction_opcode_t m_reductionOpcode;
    uint32_t                         m_syncObjectAddress = (uint32_t)-1;
    uint32_t                         m_addressMSB;
    uint32_t                         m_stride;
};
class CachedCollectiveContext
{
public:
    CachedCollectiveContext(uint8_t                           collectiveContextIndex,
                            const std::vector<unsigned>&      nicEngines,
                            const Gen2ArchServerConnectivity& serverConnectivity,
                            HclCommandsGen2Arch&              commands);
    virtual ~CachedCollectiveContext() = default;

    void dwordDiff(const RequiredCollectiveContext& other, edwords_t& dwordsForUpdate);

    void advanceSOB(edwords_t& dwordsForUpdate, unsigned& syncObjectAddressIndex, uint64_t requiredAddress);

    void addNicBufferToNicPassthroughHandler(const NicsDwordsArray& nicBuffer);
    void flushNicPassthroughHandler(hcl::ScalStreamBase& scalStream,
                                    ContextManager&      contextManager,
                                    int                  selfDevice,
                                    HCL_Comm             comm,
                                    unsigned             syncObjectAddressIndex,
                                    bool                 isSend,
                                    bool                 incSOBinNOP = true);

    struct g2fw::nic_coll_ctxt_t m_data;

    unsigned m_lastSyncObjectAddressIndex;

private:
    NicPassthroughHandler m_nicPassthroughHandler;
    uint8_t               m_collectiveContextIndex;
};

class CachedCollectiveContextScaleUp : public CachedCollectiveContext
{
public:
    CachedCollectiveContextScaleUp(uint8_t                           collectiveContextIndex,
                                   const std::vector<unsigned>&      nicEngines,
                                   const Gen2ArchServerConnectivity& serverConnectivity,
                                   HclCommandsGen2Arch&              commands);
    virtual ~CachedCollectiveContextScaleUp() = default;
    CommunicatorDescriptor m_activeCommunicatorDescriptor;
};