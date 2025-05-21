#include "platform/gaudi2/context_manager.h"

#include <string.h>                                         // for memset
#include <algorithm>                                        // for fill
#include <cstdint>                                          // for uint32_t, uint8_t
#include <memory>                                           // for allocator_trait...
#include "hcl_utils.h"                                      // for VERIFY
#include "infra/scal/gen2_arch_common/scal_stream.h"        // for ScalStreamBase
#include "hcl_log_manager.h"                                // for LOG_*
#include "platform/gaudi2/communicator_descriptor.h"        // for CommunicatorDes...
#include "platform/gaudi2/hcl_packets.h"                    // for serializeUpdate...
#include "platform/gaudi2/nic_passthrough_handler.h"        // for NicPassthroughH...
#include "platform/gaudi2/hal.h"                            // for Gaudi2Hal
#include "g2_sched_pkts.h"                                  // for g2fw
#include "platform/gen2_arch_common/types.h"                // for reduction_datat...
#include "hcl_global_conf.h"                                // for GCFG
#include "platform/gaudi2/hcl_device.h"                     // for HclDeviceGaudi2
#include "platform/gen2_arch_common/server_connectivity.h"  // for Gen2ArchServerConnectivity

class HclCommandsGen2Arch;

// workaround for assigning from a packed uint32_t
typedef uint32_t uint32_t_a __attribute__((aligned(1)));

static hcl::Gaudi2Hal s_hal;

static inline g2_nic_engine_reduction_opcode_t
getReductionOpcode(HCL_CollectiveOp collectiveOp, hcclRedOp_t reduceOp, hcclDataType_t dataType)
{
    static const std::map<hcclDataType_t, enum reduction_datatype_e> REDUCTION_MAP {
        {hcclInt8, REDUCTION_INT8},
        {hcclInt32, REDUCTION_INT32},
        {hcclUint8, REDUCTION_UINT8},
        {hcclUint32, REDUCTION_UINT32},
        {hcclBfloat16, REDUCTION_UPSCALING_BF16},
        {hcclFloat16, REDUCTION_UPSCALING_FP16},
        {hcclFloat32, REDUCTION_FP32}
    };

    g2_nic_engine_reduction_opcode_t result = {.raw = 0};
    if (dataType == hcclNumTypes)
    {
        return result;
    }

    result.datatype = REDUCTION_MAP.at(dataType);
    if (collectiveOp != eHCLReduceScatter && collectiveOp != eHCLReduce)
    {
        return result;
    }

    switch (reduceOp)
    {
        case hcclSum:
            result.indication = 1;
            result.operation  = REDUCTION_OP_ADDITION;
            break;
        case hcclMax:
            result.indication = 1;
            result.operation  = REDUCTION_OP_MAXIMUM;
            break;
        case hcclMin:
            result.indication = 1;
            result.operation  = REDUCTION_OP_MINIMUM;
            break;
        case hcclOpNone:
            result.indication = 0;
            break;
        default:
            VERIFY(false, "Invalid reductioOp value {}", reduceOp);
    }

    result.rounding         = REDUCTION_ROUND_HALF_TO_NEAREST_EVEN;
    result.operation_AxUSER = 0;

    return result;
}

UniqueCollectiveContext::UniqueCollectiveContext()
{
    remote_index       = -1;
    connection_enabled = false;
}

static inline g2fw::nic_coll_ctxt_dword_t createEmptyRemoteDescriptor()
{
    g2fw::nic_coll_ctxt_dword_t descriptor;
    descriptor.dword_value = 0;

    return descriptor;
}

RequiredCollectiveContext::RequiredCollectiveContext()
{
    m_remoteDescriptor    = createEmptyRemoteDescriptor();
    m_reductionOpcode.raw = 0;
    m_addressMSB          = 0;
    m_stride              = 0;
}

RequiredCollectiveContext::RequiredCollectiveContext(HCL_CollectiveOp collectiveOp,
                                                     hcclRedOp_t      reduceOp,
                                                     uint32_t         soAddress,
                                                     bool             isSend,
                                                     uint32_t         addressMSB,
                                                     hcclDataType_t   dataType,
                                                     uint64_t         strideCount)
{
    g2_nic_engine_reduction_opcode_t reductionOpcode = getReductionOpcode(collectiveOp, reduceOp, dataType);

    m_remoteDescriptor    = createEmptyRemoteDescriptor();
    m_reductionOpcode.raw = reductionOpcode.raw;
    m_addressMSB          = addressMSB;
    m_stride              = strideCount;
    m_syncObjectAddress   = soAddress & 0x7ffffff;
    m_syncObjectAddress |= (1 << 27) | (1 << 30);  // set CT=1, SO data = 1

    if (!isSend)
    {
        m_syncObjectAddress |= (1 << 29);  // set SC = 1 (++)
    }
}

void RequiredCollectiveContext::dwordDiff(const RequiredCollectiveContext& required, edwords_t& dwordsForUpdate)
{
    if (this->m_reductionOpcode.raw != required.m_reductionOpcode.raw)
    {
        dwordsForUpdate.DW0 = true;
    }

    if (this->m_syncObjectAddress != required.m_syncObjectAddress)
    {
        dwordsForUpdate.DW1 = true;
    }

    if (this->m_addressMSB != required.m_addressMSB)
    {
        dwordsForUpdate.DW3 = true;
    }

    if (this->m_stride != required.m_stride)
    {
        dwordsForUpdate.DW4 = true;
    }
}

CachedCollectiveContext::CachedCollectiveContext(uint8_t                           collectiveContextIndex,
                                                 const std::vector<unsigned>&      nicEngines,
                                                 const Gen2ArchServerConnectivity& serverConnectivity,
                                                 HclCommandsGen2Arch&              commands)
: m_lastSyncObjectAddressIndex(0),
  m_nicPassthroughHandler(nicEngines, serverConnectivity, commands),
  m_collectiveContextIndex(collectiveContextIndex)

{
    memset(&m_data, 0, sizeof(m_data));
}

CachedCollectiveContextScaleUp::CachedCollectiveContextScaleUp(uint8_t                           collectiveContextIndex,
                                                               const std::vector<unsigned>&      nicEngines,
                                                               const Gen2ArchServerConnectivity& serverConnectivity,
                                                               HclCommandsGen2Arch&              commands)
: CachedCollectiveContext(collectiveContextIndex, nicEngines, serverConnectivity, commands),
  m_activeCommunicatorDescriptor(collectiveContextIndex)
{
}

void CachedCollectiveContext::dwordDiff(const RequiredCollectiveContext& required, edwords_t& dwordsForUpdate)
{
    dwordsForUpdate.DW0 = m_data.reduction_opcode != required.m_reductionOpcode.raw;

    if (m_data.sync_object_address_0 != required.m_syncObjectAddress)
    {
        dwordsForUpdate.DW1 = true;
    }

    dwordsForUpdate.DW3 = m_data.buffer_addr_msb != required.m_addressMSB;

    dwordsForUpdate.DW4 = m_data.stride != required.m_stride;
}

void CachedCollectiveContext::advanceSOB([[maybe_unused]] edwords_t& dwordsForUpdate,
                                         unsigned&                   syncObjectAddressIndex,
                                         [[maybe_unused]] uint64_t   requiredAddress)
{
    syncObjectAddressIndex = m_lastSyncObjectAddressIndex;
    m_data.sync_object_address_0 += sizeof(uint32_t);
}

void CachedCollectiveContext::addNicBufferToNicPassthroughHandler(const NicsDwordsArray& nicBuffer)
{
    m_nicPassthroughHandler.addNicBuffer(nicBuffer);
}

void CachedCollectiveContext::flushNicPassthroughHandler(hcl::ScalStreamBase&             scalStream,
                                                         [[maybe_unused]] ContextManager& contextManager,
                                                         int                              selfDevice,
                                                         HCL_Comm                         comm,
                                                         unsigned                         syncObjectAddressIndex,
                                                         bool                             isSend,
                                                         bool                             incSOBinNOP)
{
    m_nicPassthroughHandler
        .flush(scalStream, m_collectiveContextIndex, selfDevice, comm, syncObjectAddressIndex, isSend, incSOBinNOP);
}

ContextManager::ContextManager(const std::vector<unsigned>& nicEngines, HclDeviceGaudi2& device)
: m_nicEngines(nicEngines), m_device(device), m_serverConnectivity(device.getServerConnectivity())
{
    std::fill(m_activeNics.begin(), m_activeNics.end(), false);
}

void ContextManager::serializeUpdateGlobalContext(hcl::ScalStreamBase& scalStream,
                                                  uint32_t             soAddressLSB,
                                                  uint64_t             intermediateBaseAddress,
                                                  unsigned             intermediateSliceSize)
{
    SchedArcCommandsGaudi2::serializeUpdateGlobalContextCommand(scalStream, soAddressLSB, m_globalContexts);

    SchedArcCommandsGaudi2::serializeUpdateGlobalContextInfo(scalStream,
                                                             soAddressLSB,
                                                             intermediateBaseAddress,
                                                             intermediateSliceSize);
}

void ContextManager::serializeUpdateGlobalContextScaleOut(hcl::ScalStreamBase& scalStream, uint32_t soAddressLSB)
{
    SchedArcCommandsGaudi2::serializeUpdateGlobalContextScaleOutCommand(scalStream,
                                                                        soAddressLSB,
                                                                        m_scaleoutGlobalContexts,
                                                                        0);
}

// update dwords that are common to scale up and scale out
void ContextManager::updateCommonDword(unsigned                         collectiveContextIndex,
                                       const RequiredCollectiveContext& requiredContext,
                                       edwords_t&                       dwordsForUpdate,
                                       ContextValues&                   contextValues,
                                       bool                             isScaleup)
{
    CachedCollectiveContext* cachedCollectiveContext = &m_cachedCollectiveContextsScaleUp[collectiveContextIndex];
    if (!isScaleup)
    {
        cachedCollectiveContext = &m_cachedCollectiveContextsScaleOut[collectiveContextIndex];
    }

    if (dwordsForUpdate.DW0)
    {
        VERIFY(requiredContext.m_reductionOpcode.raw != cachedCollectiveContext->m_data.reduction_opcode);

        updateDWord(contextValues, DW0, requiredContext.m_reductionOpcode.raw);
        cachedCollectiveContext->m_data.reduction_opcode = requiredContext.m_reductionOpcode.raw;
    }

    if (dwordsForUpdate.DW1)
    {
        VERIFY(requiredContext.m_syncObjectAddress != cachedCollectiveContext->m_data.sync_object_address_0);

        updateDWord(contextValues, DW1, requiredContext.m_syncObjectAddress);
        cachedCollectiveContext->m_data.sync_object_address_0 = requiredContext.m_syncObjectAddress;
    }

    if (dwordsForUpdate.DW2)
    {
        VERIFY(requiredContext.m_syncObjectAddress != cachedCollectiveContext->m_data.sync_object_address_1);

        updateDWord(contextValues, DW2, requiredContext.m_syncObjectAddress);
        cachedCollectiveContext->m_data.sync_object_address_1 = requiredContext.m_syncObjectAddress;
    }

    if (dwordsForUpdate.DW3)
    {
        VERIFY(requiredContext.m_addressMSB != cachedCollectiveContext->m_data.buffer_addr_msb);

        updateDWord(contextValues, DW3, requiredContext.m_addressMSB);
        cachedCollectiveContext->m_data.buffer_addr_msb = requiredContext.m_addressMSB;
    }

    if (dwordsForUpdate.DW4)
    {
        VERIFY(requiredContext.m_stride != cachedCollectiveContext->m_data.stride);

        updateDWord(contextValues, DW4, requiredContext.m_stride);
        cachedCollectiveContext->m_data.stride = requiredContext.m_stride;
    }
}

void ContextManager::updateDWord(ContextValues& contextValues, eDWords dw, uint32_t newValue)
{
    (contextValues.first).at(dw).needUpdate = true;
    (contextValues.first).at(dw).value      = newValue;
    contextValues.second++;
}

inline uint32_t ContextManager::idx2qpi(unsigned ctxIndex)
{
    // (ctxIndex % 2) == 1 : Even-numbered are RS, Odd-numbered are AG
    const HCL_CollectiveOp collectiveOp = (ctxIndex % 2) == 1 ? eHCLAllGather : eHCLReduceScatter;

    // ctxIndex < (s_hal.getCollectiveContextsCount() / 2) : 0-7 are Recv contexts, 8-15 are Send contexts
    const bool isSend = (ctxIndex >= (s_hal.getCollectiveContextsCount() / 2));

    return m_device.getCollectiveQpi(collectiveOp, isSend);
}

void ContextManager::serializeUpdateCollectiveContextScaleUp(hcl::ScalStreamBase&             scalStream,
                                                             unsigned                         selfModuleId,
                                                             bool                             isSend,
                                                             unsigned                         collectiveContextIndex,
                                                             HCL_Comm                         comm,
                                                             [[maybe_unused]] bool            isAllGather,
                                                             const RequiredCollectiveContext& requiredContext,
                                                             edwords_t&                       dwordsForUpdate,
                                                             unsigned&                        syncObjectAddressIndex,
                                                             unsigned&                        commDescIndex,
                                                             bool                             isScaleup)
{
    std::vector<CachedCollectiveContextScaleUp>& cache                   = m_cachedCollectiveContextsScaleUp;
    CachedCollectiveContextScaleUp&              cachedCollectiveContext = cache[collectiveContextIndex];
    ContextValues                                contextValues           = {};

    updateCommonDword(collectiveContextIndex, requiredContext, dwordsForUpdate, contextValues, isScaleup);

    // Mapping between {commDescIndex, QP} and a list of NICs needing this QP.
    std::map<std::pair<unsigned, uint32_t>, std::vector<uint8_t>> commDescWithQPs = {};
    if (cachedCollectiveContext.m_activeCommunicatorDescriptor.requiresLruUpdate(comm) ||
        dwordsForUpdate.DW_REMOTE_RANK)
    {
        // If 'requiresLruUpdate()' returned true, it means that the LRU cache is full and thus we care to 'useQP' each
        // time (otherwise, the LRU is not full, and we don't want to pay the overhead of updating the LRU each and
        // every time).
        // We're going to get the (commDescIndex, QPN) for each and every NIC and determine which command to use to
        // update the QP - a normal 'update collective context' (for the single QPN case) or a 'nic passthrough' (for
        // the multiple QPNs case).
        for (uint8_t nic = 0; nic < MAX_NICS_GEN2ARCH; nic++)
        {
            if (m_activeNics[nic] == false) continue;

            const QPManagerHints hints(comm, HCL_INVALID_RANK, nic, idx2qpi(collectiveContextIndex));

            std::pair<unsigned, uint32_t> result = cachedCollectiveContext.m_activeCommunicatorDescriptor.useQP(
                comm,
                m_device.getComm(comm).m_qpManagers[nic]->getQPn(hints));
            commDescWithQPs[result].push_back(nic);  // make active, get QPs
        }

        const std::pair<unsigned, uint32_t>& singleResult = commDescWithQPs.begin()->first;
        if (dwordsForUpdate.DW_COMM_QP && commDescWithQPs.size() == 1)
        {
            updateDWord(contextValues, DW_COMM_QP, singleResult.second);
            cachedCollectiveContext.m_activeCommunicatorDescriptor.markCommDownload(comm);
        }
        commDescIndex = singleResult.first;

        // If we were asked to update the RRI, we should also get it and add it to the list.
        if (dwordsForUpdate.DW_REMOTE_RANK)
        {
            for (uint8_t nic = 0; nic < MAX_NICS_GEN2ARCH; nic++)
            {
                if (m_activeNics[nic] == false) continue;

                g2fw::nic_coll_ctxt_dword_t& currentRemoteDescriptor =
                    cachedCollectiveContext.m_activeCommunicatorDescriptor.getRemoteDescriptor(comm, nic);
                currentRemoteDescriptor = requiredContext.m_remoteDescriptor;
            }

            updateDWord(contextValues, DW_REMOTE_RANK, (uint32_t_a)requiredContext.m_remoteDescriptor.dword_value);
        }
    }
    else
    {
        // LRU is not full - just get the comm desc index, don't update the LRU.
        commDescIndex = cachedCollectiveContext.m_activeCommunicatorDescriptor.getCommDescIndex(comm);
    }

    SchedArcCommandsGaudi2::serializeUpdateCollectiveContextCommand(scalStream,
                                                                    isSend,
                                                                    collectiveContextIndex,
                                                                    commDescIndex,
                                                                    contextValues);

    if (dwordsForUpdate.DW_COMM_QP && commDescWithQPs.size() > 1)
    {
        serializeMultipleQPsUpdateScaleUp(scalStream,
                                          commDescWithQPs,
                                          selfModuleId,
                                          isSend,
                                          collectiveContextIndex,
                                          comm,
                                          syncObjectAddressIndex,
                                          commDescIndex,
                                          isScaleup);
    }
}

// this function is used only for scale up
void ContextManager::serializeMultipleQPsUpdateScaleUp(
    hcl::ScalStreamBase&                                           scalStream,
    std::map<std::pair<unsigned, uint32_t>, std::vector<uint8_t>>& commDescWithQPs,
    unsigned                                                       selfModuleId,
    bool                                                           isSend,
    unsigned                                                       collectiveContextIndex,
    HCL_Comm                                                       comm,
    unsigned&                                                      syncObjectAddressIndex,
    [[maybe_unused]] unsigned&                                     commDescIndex,
    [[maybe_unused]] bool                                          isScaleup)
{
    NicsDwordsArray buffer;

    for (auto& kvPair : commDescWithQPs)
    {
        unsigned              commDescIdx = kvPair.first.first;
        unsigned              qpn         = kvPair.first.second;
        std::vector<uint8_t>& nics        = kvPair.second;

        hcl::ScalStreamBase tmp;
        ContextValues       contextValues = {};
        updateDWord(contextValues, DW_COMM_QP, qpn);
        SchedArcCommandsGaudi2::serializeUpdateCollectiveContextCommand(tmp,
                                                                        isSend,
                                                                        collectiveContextIndex,
                                                                        commDescIdx,
                                                                        contextValues);
        // Only one command should persist - an Update Collective Command with 3 dwords. The first dword is consumed
        // by the SARC and can be ignored.
        VERIFY(tmp.m_buffer.size() == 3);

        for (uint8_t nic : nics)
        {
            const uint32_t* commandsBuffer = reinterpret_cast<const uint32_t*>(tmp.m_buffer.data());
            buffer[nic].push_back(commandsBuffer[1]);
            buffer[nic].push_back(commandsBuffer[2]);
        }
    }
    std::vector<CachedCollectiveContextScaleUp>& cache                   = m_cachedCollectiveContextsScaleUp;
    CachedCollectiveContext&                     cachedCollectiveContext = cache.at(collectiveContextIndex);

    cachedCollectiveContext.addNicBufferToNicPassthroughHandler(buffer);
    cachedCollectiveContext
        .flushNicPassthroughHandler(scalStream, *this, selfModuleId, comm, syncObjectAddressIndex, isSend, false);
}

void ContextManager::createCollectiveContexts(HclCommandsGen2Arch& commands, const HCL_Comm hclCommId)
{
    m_maxNics               = s_hal.getMaxNics();
    m_maxCollectiveContexts = s_hal.getCollectiveContextsCount();

    for (int nic = 0; nic < m_maxNics; nic++)
    {
        g2fw::nic_glbl_ctxt_t globalContext;
        std::memset(&globalContext, 0, sizeof(globalContext));
        globalContext.remote_dev_idx  = m_serverConnectivity.getRemoteDevice(nic, hclCommId);
        globalContext.sub_nic_idx     = m_serverConnectivity.getSubPortIndex(nic, hclCommId);
        globalContext.is_valid        = 1;
        globalContext.ports_per_rank  = m_serverConnectivity.getMaxNumScaleUpPortsPerConnection();
        globalContext.total_nic_count = m_serverConnectivity.getNumScaleUpPorts(hclCommId);  // scaleup
        m_globalContexts.push_back(globalContext);
    }

    // Update global context to inform FW which scale out ports should be used
    // global contexts for all scaleout nics must be updated (even for nics that
    // are disabled and will not participate in the scaleout operations)
    for (unsigned nic_idx = 0; nic_idx < m_serverConnectivity.getMaxNumScaleOutPorts(); nic_idx++)
    {
        g2fw::nic_glbl_ctxt_t scaleoutGlobalContext;
        std::memset(&scaleoutGlobalContext, 0, sizeof(scaleoutGlobalContext));
        scaleoutGlobalContext.ports_per_rank  = m_serverConnectivity.getNumScaleOutPortsGlbl(hclCommId);
        scaleoutGlobalContext.total_nic_count = m_serverConnectivity.getNumScaleOutPortsGlbl(hclCommId);
        scaleoutGlobalContext.sub_nic_idx     = m_serverConnectivity.getScaleoutSubPortIndexGlbl(
            m_serverConnectivity.getDefaultScaleOutPortByIndex(nic_idx),
            hclCommId);
        scaleoutGlobalContext.is_valid = 1;
        m_scaleoutGlobalContexts.push_back(scaleoutGlobalContext);
    }

    for (int i = 0; i < m_maxCollectiveContexts; i++)
    {
        m_cachedCollectiveContextsScaleUp.push_back(
            CachedCollectiveContextScaleUp(i, m_nicEngines, m_serverConnectivity, commands));
        m_cachedCollectiveContextsScaleOut.push_back(
            CachedCollectiveContextScaleOut(i, m_nicEngines, m_serverConnectivity, commands));
    }
}

void ContextManager::registerEarc(HCL_Comm comm, int nic)  // 8 (subnic0), 22 (subnic1), 23 (subnic2) for scaleout
{
    g2fw::nic_glbl_ctxt_t& globalContext = m_globalContexts.at(nic);
    globalContext.is_valid               = 1;

    if (!m_device.isScaleOutPort(nic, comm))
    {
        m_activeNics[nic] = true;

        for (auto& collectiveContextScaleUp : m_cachedCollectiveContextsScaleUp)
        {
            collectiveContextScaleUp.m_activeCommunicatorDescriptor.registerComm(comm);
        }
    }
}

uint16_t ContextManager::getRemoteRankQp(unsigned collectiveContextIndex,
                                         HCL_Comm comm,
                                         HCL_Rank remoteRank,
                                         int      nic,
                                         uint8_t  qpSet)
{
    const QPManagerHints hints(comm, remoteRank, nic, idx2qpi(collectiveContextIndex), INVALID_QP, qpSet);

    return m_device.getComm(comm).m_qpManagers[nic]->getQPn(hints);
}

g2fw::nic_coll_ctxt_dword_t
ContextManager::createRemoteDescriptor(const std::array<UniqueCollectiveContext, HLS2_BOX_SIZE>& uniqueContexts)
{
    g2fw::nic_coll_ctxt_dword_t descriptor = createEmptyRemoteDescriptor();

    for (unsigned device = 0; device < uniqueContexts.size(); device++)
    {
        const UniqueCollectiveContext& uniqueContext = uniqueContexts[device];

        if (uniqueContext.connection_enabled == false) continue;

        switch (device)
        {
            case 0:
                descriptor.remote_rank_index0  = uniqueContext.remote_index;
                descriptor.remote_rank_enable0 = 1;
                break;
            case 1:
                descriptor.remote_rank_index1  = uniqueContext.remote_index;
                descriptor.remote_rank_enable1 = 1;
                break;
            case 2:
                descriptor.remote_rank_index2  = uniqueContext.remote_index;
                descriptor.remote_rank_enable2 = 1;
                break;
            case 3:
                descriptor.remote_rank_index3  = uniqueContext.remote_index;
                descriptor.remote_rank_enable3 = 1;
                break;
            case 4:
                descriptor.remote_rank_index4  = uniqueContext.remote_index;
                descriptor.remote_rank_enable4 = 1;
                break;
            case 5:
                descriptor.remote_rank_index5  = uniqueContext.remote_index;
                descriptor.remote_rank_enable5 = 1;
                break;
            case 6:
                descriptor.remote_rank_index6  = uniqueContext.remote_index;
                descriptor.remote_rank_enable6 = 1;
                break;
            case 7:
                descriptor.remote_rank_index7  = uniqueContext.remote_index;
                descriptor.remote_rank_enable7 = 1;
                break;
            default:
                VERIFY(false, "Invalid device {}", device);
        }
    }

    return descriptor;
}

#define CONFIG_REMOTE_DESCRIPTOR(X)                                                                                    \
    result[X].connection_enabled = context.m_remoteDescriptor.remote_rank_enable##X;                                   \
    result[X].remote_index       = context.m_remoteDescriptor.remote_rank_index##X;

std::array<UniqueCollectiveContext, HLS2_BOX_SIZE>
ContextManager::createUniqueContexts(const RequiredCollectiveContext& context)
{
    std::array<UniqueCollectiveContext, HLS2_BOX_SIZE> result;

    CONFIG_REMOTE_DESCRIPTOR(0);
    CONFIG_REMOTE_DESCRIPTOR(1);
    CONFIG_REMOTE_DESCRIPTOR(2);
    CONFIG_REMOTE_DESCRIPTOR(3);
    CONFIG_REMOTE_DESCRIPTOR(4);
    CONFIG_REMOTE_DESCRIPTOR(5);
    CONFIG_REMOTE_DESCRIPTOR(6);
    CONFIG_REMOTE_DESCRIPTOR(7);

    return result;
}

edwords_t ContextManager::getDwordsForUpdate(bool                             isScaleup,
                                             unsigned                         collectiveContextIndex,
                                             HCL_Comm                         comm,
                                             const RequiredCollectiveContext& requiredContext)
{
    // These are the 'first' nics (the nics with sub-nic-index == 0) of each device. We only need to iterate through
    // them and not through all the NICs (improves function runtime by two-thirds).
    static const uint8_t nics[] = {0, 2, 6, 10, 13, 16, 19};

    CachedCollectiveContext* cachedCollectiveContext = &m_cachedCollectiveContextsScaleUp[collectiveContextIndex];
    if (!isScaleup)
    {
        cachedCollectiveContext = &m_cachedCollectiveContextsScaleOut[collectiveContextIndex];
    }

    edwords_t dwordsForUpdate;

    cachedCollectiveContext->dwordDiff(requiredContext, dwordsForUpdate);

    if (isScaleup)
    {
        CachedCollectiveContextScaleUp* cachedCollectiveContextScaleUp =
            (CachedCollectiveContextScaleUp*)cachedCollectiveContext;
        for (uint8_t nic : nics)
        {
            if (m_activeNics[nic] == false) continue;

            if (!cachedCollectiveContextScaleUp->m_activeCommunicatorDescriptor.isActive(comm))
            {
                dwordsForUpdate.DW_COMM_QP = true;
            }
            g2fw::nic_coll_ctxt_dword_t& currentRemoteDescriptor =
                cachedCollectiveContextScaleUp->m_activeCommunicatorDescriptor.getRemoteDescriptor(comm, nic);

            if (requiredContext.m_remoteDescriptor.dword_value != currentRemoteDescriptor.dword_value)
            {
                dwordsForUpdate.DW_REMOTE_RANK = true;
            }

            if (dwordsForUpdate.DW_COMM_QP || dwordsForUpdate.DW_REMOTE_RANK) break;
        }
    }

    if (dwordsForUpdate != 0)
    {
        LOG_HCL_INFO(HCL, "diff for collective context {}: {:b}", collectiveContextIndex, (uint64_t)dwordsForUpdate);
    }

    return dwordsForUpdate;
}

void ContextManager::updateCollectiveContextScaleUp(hcl::ScalStreamBase&             scalStream,
                                                    unsigned                         selfModuleId,
                                                    bool                             isSend,
                                                    unsigned                         collectiveContextIndex,
                                                    HCL_Comm                         comm,
                                                    bool                             isAllGather,
                                                    const RequiredCollectiveContext& requiredContext,
                                                    edwords_t*                       pDwordsForUpdate,
                                                    unsigned&                        syncObjectAddressIndex,
                                                    unsigned&                        commDescIndex)
{
    edwords_t dwordsForUpdate =
        pDwordsForUpdate ? *pDwordsForUpdate : getDwordsForUpdate(true, collectiveContextIndex, comm, requiredContext);

    if (dwordsForUpdate > 0)
    {
        serializeUpdateCollectiveContextScaleUp(scalStream,
                                                selfModuleId,
                                                isSend,
                                                collectiveContextIndex,
                                                comm,
                                                isAllGather,
                                                requiredContext,
                                                dwordsForUpdate,
                                                syncObjectAddressIndex,
                                                commDescIndex,
                                                true);
    }
    else
    {
        CachedCollectiveContextScaleUp& cachedCollectiveContext =
            m_cachedCollectiveContextsScaleUp[collectiveContextIndex];
        if (m_cachedCollectiveContextsScaleUp[collectiveContextIndex].m_activeCommunicatorDescriptor.requiresLruUpdate(
                comm))
        {
            for (uint8_t nic = 0; nic < MAX_NICS_GEN2ARCH; nic++)
            {
                if (m_activeNics[nic] == false) continue;

                const QPManagerHints hints(comm, HCL_INVALID_RANK, nic, idx2qpi(collectiveContextIndex));

                std::pair<unsigned, uint32_t> result = cachedCollectiveContext.m_activeCommunicatorDescriptor.useQP(
                    comm,
                    m_device.getComm(comm).m_qpManagers[nic]->getQPn(hints));
                commDescIndex = result.first;
                cachedCollectiveContext.m_activeCommunicatorDescriptor.markCommDownload(comm);
            }
        }
        else
        {
            commDescIndex = cachedCollectiveContext.m_activeCommunicatorDescriptor.getCommDescIndex(comm);
        }
    }

    m_cachedCollectiveContextsScaleUp[collectiveContextIndex].advanceSOB(dwordsForUpdate,
                                                                         syncObjectAddressIndex,
                                                                         requiredContext.m_syncObjectAddress);
}

void ContextManager::updateCollectiveContextScaleOut(unsigned                         collectiveContextIndex,
                                                     const RequiredCollectiveContext& requiredContext,
                                                     edwords_t&                       dwordsForUpdate,
                                                     unsigned&                        syncObjectAddressIndex,
                                                     ContextValues&                   contextValues)
{
    if (dwordsForUpdate > 0)
    {
        updateCommonDword(collectiveContextIndex, requiredContext, dwordsForUpdate, contextValues, false);
    }

    m_cachedCollectiveContextsScaleOut[collectiveContextIndex].advanceSOB(dwordsForUpdate,
                                                                          syncObjectAddressIndex,
                                                                          requiredContext.m_syncObjectAddress);
}
