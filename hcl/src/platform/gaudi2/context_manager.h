#pragma once

#include <cstdint>
#include <vector>
#include <array>                                   // for array
#include <map>                                     // for map
#include <set>                                     // for set
#include <utility>                                 // for pair
#include "hcl_api_types.h"                         // for HCL_Comm, HCL_Rank
#include "platform/gaudi2/types.h"                 // for eDWords, HLS2_BOX_...
#include "sched_pkts.h"                            // for g2fw
#include "interfaces/hcl_idevice.h"                // for IHclDevice
#include "platform/gaudi2/context_manager_priv.h"

class Gaudi2DevicePortMapping;
class HclCommandsGen2Arch;

constexpr uint INVALID_QP = 0;

namespace hcl
{
class ScalStreamBase;
}

enum G2QP_e  // QP index descritptor
{
    QPE_RS_RECV = 0,
    QPE_AG_RECV,
    QPE_RS_SEND,
    QPE_AG_SEND,
};

class ContextManager
{
public:
    ContextManager(const std::vector<unsigned>& nicEngines, Gaudi2DevicePortMapping& portMapping, IHclDevice& device);
    virtual ~ContextManager() = default;
    uint32_t getQpi(HCL_Comm comm, uint8_t nic, HCL_Rank remoteRank, uint32_t qpn, uint8_t qpSet);

    void createCollectiveContexts(HclCommandsGen2Arch& commands);
    void registerEarc(HCL_Comm comm, int nic, HCL_Rank remoteRank, const QpsVector& qps, const bool isPeer = true);
    void allocateScaleOutContext(HCL_Comm comm, uint32_t commSize);

    uint16_t
    getRemoteRankQp(unsigned collectiveContextIndex, HCL_Comm comm, HCL_Rank remoteRank, int nic, uint8_t qpSet);
    uint16_t
    getRemoteRankQpi(unsigned collectiveContextIndex, HCL_Comm comm, HCL_Rank remoteRank, int nic, uint8_t qpSet);

    void serializeUpdateGlobalContext(hcl::ScalStreamBase& scalStream,
                                      uint32_t             soAddressLSB,
                                      uint64_t             intermediateBaseAddress = 0,
                                      unsigned             intermediatSliceSize    = 0);

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

    void updateCollectiveContextScaleOut(unsigned                         collectiveContextIndex,
                                         const RequiredCollectiveContext& requiredContext,
                                         edwords_t&                       dwordsForUpdate,
                                         unsigned&                        syncObjectAddressIndex,
                                         ContextValues&                   contextValues);
    Gaudi2DevicePortMapping& m_portMapping;

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

    const std::vector<unsigned> m_nicEngines;

    std::vector<g2fw::nic_glbl_ctxt_t>           m_globalContexts;                    // one per EARC
    std::vector<g2fw::nic_glbl_ctxt_t>           m_scaleoutGlobalContexts;            // one per EARC
    std::vector<CachedCollectiveContextScaleUp>  m_cachedCollectiveContextsScaleUp;   // one per Collective Context
    std::vector<CachedCollectiveContextScaleOut> m_cachedCollectiveContextsScaleOut;  // one per Collective Context

    std::array<bool, MAX_NICS_GEN2ARCH> m_activeNics;

    int m_maxNics               = -1;
    int m_maxCollectiveContexts = -1;
    IHclDevice& m_device;
};
