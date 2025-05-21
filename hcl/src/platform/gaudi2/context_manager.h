#pragma once

#include <cstdint>
#include <vector>
#include <array>                    // for array
#include <map>                      // for map
#include <set>                      // for set
#include <utility>                  // for pair
#include "hcl_api_types.h"          // for HCL_Comm, HCL_Rank
#include "platform/gaudi2/types.h"  // for eDWords, HLS2_BOX_...
#include "g2_sched_pkts.h"          // for g2fw
#include "platform/gaudi2/context_manager_priv.h"
#include "platform/gaudi2/qp_manager.h"
#include "platform/gaudi2/hcl_device.h"                           // for HclDeviceGaudi2
#include "platform/gen2_arch_common/server_connectivity.h"        // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/server_connectivity_types.h"  // for DEFAULT_COMM_ID

class HclCommandsGen2Arch;

namespace hcl
{
class ScalStreamBase;
}

class ContextManager
{
public:
    ContextManager(const std::vector<unsigned>& nicEngines, HclDeviceGaudi2& device);
    virtual ~ContextManager() = default;

    void createCollectiveContexts(HclCommandsGen2Arch& commands, const HCL_Comm hclCommId = DEFAULT_COMM_ID);
    void registerEarc(HCL_Comm comm, int nic);

    uint16_t
    getRemoteRankQp(unsigned collectiveContextIndex, HCL_Comm comm, HCL_Rank remoteRank, int nic, uint8_t qpSet);

    void serializeUpdateGlobalContext(hcl::ScalStreamBase& scalStream,
                                      uint32_t             soAddressLSB,
                                      uint64_t             intermediateBaseAddress = 0,
                                      unsigned             intermediateSliceSize   = 0);

    void serializeUpdateGlobalContextScaleOut(hcl::ScalStreamBase& scalStream, uint32_t soAddressLSB);

    static g2fw::nic_coll_ctxt_dword_t
    createRemoteDescriptor(const std::array<UniqueCollectiveContext, HLS2_BOX_SIZE>& uniqueContexts);
    static std::array<UniqueCollectiveContext, HLS2_BOX_SIZE>
    createUniqueContexts(const RequiredCollectiveContext& context);

    edwords_t getDwordsForUpdate(bool                             isScaleup,
                                 unsigned                         collectiveContextIndex,
                                 HCL_Comm                         comm,
                                 const RequiredCollectiveContext& requiredContext);

    struct ContextValueUpdater
    {
        bool     needUpdate;
        uint32_t value;
        ContextValueUpdater()
        {
            needUpdate = false;
            value      = 0;
        }
    };
    typedef std::pair<std::array<ContextValueUpdater, DW_NUM>, int>
        ContextValues;  // pair.second is the number of dwords that need updating

    void updateCollectiveContextScaleUp(hcl::ScalStreamBase&             scalStream,
                                        unsigned                         selfModuleId,
                                        bool                             isSend,
                                        unsigned                         collectiveContextIndex,
                                        HCL_Comm                         comm,
                                        bool                             isAllGather,
                                        const RequiredCollectiveContext& requiredContext,
                                        edwords_t*                       dwordsForUpdate,
                                        unsigned&                        syncObjectAddressIndex,
                                        unsigned&                        commDescIndex);

    void                              updateCollectiveContextScaleOut(unsigned                         collectiveContextIndex,
                                                                      const RequiredCollectiveContext& requiredContext,
                                                                      edwords_t&                       dwordsForUpdate,
                                                                      unsigned&                        syncObjectAddressIndex,
                                                                      ContextValues&                   contextValues);
    const Gen2ArchServerConnectivity& getServerConnectivity() const { return m_serverConnectivity; }

private:
    void updateCommonDword(unsigned                         collectiveContextIndex,
                           const RequiredCollectiveContext& requiredContext,
                           edwords_t&                       dwordsForUpdate,
                           ContextValues&                   contextValues,
                           bool                             isScaleup);
    void updateDWord(ContextValues& contextValues, eDWords dw, uint32_t newValue);

    void serializeUpdateCollectiveContextScaleUp(hcl::ScalStreamBase&             scalStream,
                                                 unsigned                         selfModuleId,
                                                 bool                             isSend,
                                                 unsigned                         collectiveContextIndex,
                                                 HCL_Comm                         comm,
                                                 bool                             isAllGather,
                                                 const RequiredCollectiveContext& requiredContext,
                                                 edwords_t&                       dwordsForUpdate,
                                                 unsigned&                        syncObjectAddressIndex,
                                                 unsigned&                        commDescIndex,
                                                 bool                             isScaleup);

    void
    serializeMultipleQPsUpdateScaleUp(hcl::ScalStreamBase&                                           scalStream,
                                      std::map<std::pair<unsigned, uint32_t>, std::vector<uint8_t>>& commDescWithQPs,
                                      unsigned                                                       selfModuleId,
                                      bool                                                           isSend,
                                      unsigned  collectiveContextIndex,
                                      HCL_Comm  comm,
                                      unsigned& syncObjectAddressIndex,
                                      unsigned& commDescIndex,
                                      bool      isScaleup);

    uint32_t idx2qpi(unsigned ctxIndex);

    const std::vector<unsigned> m_nicEngines;

    using CachedCollectiveContextScaleOut = CachedCollectiveContext;

    std::vector<g2fw::nic_glbl_ctxt_t>           m_globalContexts;                    // one per EARC
    std::vector<g2fw::nic_glbl_ctxt_t>           m_scaleoutGlobalContexts;            // one per EARC
    std::vector<CachedCollectiveContextScaleUp>  m_cachedCollectiveContextsScaleUp;   // one per Collective Context
    std::vector<CachedCollectiveContextScaleOut> m_cachedCollectiveContextsScaleOut;  // one per Collective Context

    std::array<bool, MAX_NICS_GEN2ARCH> m_activeNics;

    int                               m_maxNics               = -1;
    int                               m_maxCollectiveContexts = -1;
    HclDeviceGaudi2&                  m_device;
    const Gen2ArchServerConnectivity& m_serverConnectivity;
};
