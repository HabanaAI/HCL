#pragma once

#include <memory>  // for unique_ptr

#include "hcl_utils.h"
#include "hcl_api_types.h"
#include "hcl_types.h"
#include "hcl_public_streams.h"
#include "hcl_dynamic_communicator.h"

struct HclCollectiveParams
{
    explicit HclCollectiveParams(HCL_CollectiveOp        collectiveOp,
                                 synStreamHandle         streamHandle,
                                 uint64_t                sendBufferAddr,
                                 uint64_t                recvBufferAddr,
                                 uint64_t                count,
                                 hcclDataType_t          dataType,
                                 HclDynamicCommunicator& dynamicComm,
                                 uint8_t                 apiId           = HCL_DEFAULT_API_ID,
                                 const uint32_t          userFlags       = 0,
                                 hcclRedOp_t             reduceOp        = hcclOpNone,
                                 HCL_Rank                root            = HCL_INVALID_RANK,
                                 uint64_t                remainder_count = 0)
    : m_collectiveOp(collectiveOp),
      m_streamHandle(streamHandle),
      m_sendBufferAddr(sendBufferAddr),
      m_recvBufferAddr(recvBufferAddr),
      m_count(count),
      m_dataType(dataType),
      m_comm(dynamicComm),
      m_apiId(apiId),
      m_userFlags(userFlags),
      m_reduceOp(reduceOp),
      m_root(root),
      m_remainder_count(remainder_count),
      m_dynamicComm(dynamicComm)
    {
    }

    // used by Gen2 only
    explicit HclCollectiveParams(HclDynamicCommunicator& dynamicComm)
    : m_collectiveOp(eHCLNoCollective),
      m_streamHandle(nullptr),
      m_sendBufferAddr(0x0),
      m_recvBufferAddr(0x0),
      m_count(0x0),
      m_dataType(hcclFloat32),
      m_comm(dynamicComm),
      m_userFlags(0),
      m_reduceOp(hcclOpNone),
      m_root(dynamicComm.getMyRank()),
      m_remainder_count(0),
      m_dynamicComm(dynamicComm)
    {
    }

    // Note there is implicit copy ctor used throughout the code
    // HclCollectiveParams(const &HclCollectiveParams)

    virtual ~HclCollectiveParams() = default;

    HCL_CollectiveOp m_collectiveOp           = eHCLReduce;
    HCL_CollectiveOp m_currentOp              = eHCLReduce;
    synStreamHandle  m_streamHandle           = 0;
    uint64_t         m_sendBufferAddr         = 0;
    uint64_t         m_recvBufferAddr         = 0;
    uint64_t         m_count                  = 0;
    hcclDataType_t   m_dataType               = hcclFloat32;
    uint64_t         m_intermediateBufferAddr = 0;
    HCL_Comm         m_comm;
    uint8_t          m_apiId           = HCL_DEFAULT_API_ID;
    uint32_t         m_userFlags       = 0;
    hcclRedOp_t      m_reduceOp        = hcclOpNone;
    HCL_Rank         m_root            = HCL_INVALID_RANK;
    uint64_t         m_remainder_count = 0;

    HclDynamicCommunicator& m_dynamicComm;
};

using HclCollectiveParamsPtr = std::unique_ptr<HclCollectiveParams>;
