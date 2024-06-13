#pragma once

#include <cstddef>                   // for NULL
#include <cstdint>                   // for uint32_t, uint8_t, uint64_t
#include <set>                       // for set
#include <unordered_map>             // for unordered_map
#include <unordered_set>             // for unordered_set

#include "hl_logger/hllog_core.hpp"  // for logger

#include "platform/gen2_arch_common/eth_stats.hpp"  // EthStats
#include "hcl_types.h"
#include "interfaces/hcl_idevice.h"  // for IHclDevice
#include "hcl_api_types.h"           // for HCL_Comm, HCL_Rank
#include "hccl_types.h"              // for hcclResult_t
#include "types.h"
#include "platform/gen2_arch_common/port_mapping_config.h"  // for Gen2ArchPortMappingConfig
#include "platform/gen2_arch_common/scaleout_provider.h"

class Gen2ArchDevicePortMapping;
class HclCommandsGen2Arch;
class HclDeviceControllerGen2Arch;
class HclConfig;
class HclDeviceConfig;
class IEventQueueHandler;
struct hlthunk_requester_conn_ctx;
struct hlthunk_responder_conn_ctx;
class DeviceBufferManager;

namespace hcl
{
class IntermediateBufferContainer;
class Gen2ArchScalManager;
};  // namespace hcl

class HclDeviceGen2Arch : public IHclDevice
{
public:
    HclDeviceGen2Arch(HclDeviceControllerGen2Arch& controller);  // for test only
    HclDeviceGen2Arch(HclDeviceControllerGen2Arch& controller, HclDeviceConfig& deviceConfig);
    virtual ~HclDeviceGen2Arch() noexcept(false);

    virtual nics_mask_t  getActiveNics(HCL_Rank fromRank, HCL_Rank toRank, int physicalQueueOffset, HCL_Comm comm) override;
    virtual nics_mask_t  getAllPorts(int deviceId, unsigned spotlightType = DEFAULT_SPOTLIGHT) = 0;
    virtual hcclResult_t onNewCommStart(HCL_Comm comm, uint32_t commSize, HclConfig& config) override;
    virtual hcclResult_t destroyComm(HCL_Comm comm, bool force = false) override;

    virtual hcclResult_t destroy(bool force = false) override;

    virtual bool                             isDramAddressValid(uint64_t addr) const override;
    hcl::Gen2ArchScalManager&                getScalManager();
    virtual const Gen2ArchDevicePortMapping& getPortMapping()      = 0;
    virtual void                             updateDisabledPorts() = 0;
    /**
     * @brief Opens QPs to remote (normally non-peers) ranks if not already opened
     *        Avoid deadlocks when communicating with more then 1 remote rank by doing first
     *        async recv from all remotes and then doing send to all remotes.
     *
     * @param comm          [in] The communicator the rank is part of
     * @param remoteRanks   [in] The list of remote rank ids
     * @return
     */
    void openAllRequiredNonPeerQPs(const HCL_Comm comm, const std::set<HCL_Rank>& remoteRanks);

    virtual uint32_t createQp(uint32_t port, uint8_t qpId) override;

    void                         updateRankHasQp(const HCL_Comm comm, const HCL_Rank remoteRank);
    DeviceBufferManager&         getSIB(const uint32_t streamIndex);
    uint64_t                     getSIBBufferSize() const;
    virtual void                 getLagInfo(int nic, uint8_t& lagIdx, uint8_t& lastInLag, unsigned spotlightType);
    virtual hcclResult_t         openQpsHlsScaleOut(HCL_Comm comm, const UniqueSortedVector& outerRanks) = 0;
    virtual HclCommandsGen2Arch& getGen2ArchCommands();
    virtual uint64_t             getEnabledPortsMask();
    ScaleoutProvider*            getScaleOutProvider();
    const std::set<HCL_Rank>&    getOpenScaleOutRanks(const HCL_Comm comm);
    unsigned                     getEdmaEngineWorkDistributionSize();
    uint8_t                      getNumQpSets(bool isScaleOut, HCL_Comm comm, HCL_Rank remoteRank);

    hcl::Gen2ArchScalManager&         m_scalManager;
    hcl::IntermediateBufferContainer* m_sibContainer = nullptr;
    synDeviceType                     m_deviceType;

    unsigned                          edmaEngineGroupSizes[2] = {
        0,
    };

    virtual void destroyQp(uint32_t port, uint32_t qpn) override;
    void         dfa(hl_logger::LoggerSPtr logger);

    /**
     * @brief Maps all HBM allocated by HCL to a dmabuf
     *
     * @param device
     * @return hcclResult_t
     */
    void exportHBMMR();
    bool isACcbHalfFullForDeviceBenchMark(const unsigned archStreamIdx);

    virtual uint64_t getDRAMSize() override;
    virtual uint64_t getDRAMBaseAddr() override;

protected:
    virtual void updateRequesterContext(hlthunk_requester_conn_ctx& req_ctx,
                                        HCL_Comm                    comm,
                                        uint8_t                     nic,
                                        HCL_Rank                    remoteRank,
                                        uint32_t                    qpn,
                                        uint8_t                     qpSet) override;

    virtual void updateResponderContext(hlthunk_responder_conn_ctx& res_ctx,
                                        HCL_Comm                    comm,
                                        uint8_t                     nic,
                                        HCL_Rank                    remoteRank,
                                        uint32_t                    qpn,
                                        uint8_t                     qpSet) override;

    virtual void registerQps(HCL_Comm comm, HCL_Rank remoteRank, const QpsVector& qps, int nic = INVALID_NIC) = 0;

    virtual uint32_t getQpi(HCL_Comm comm, uint8_t nic, HCL_Rank remoteRank, uint32_t qpn, uint8_t qpSet) = 0;
    virtual uint32_t getDestQpi(unsigned _qpi)                                             = 0;
    virtual bool     isSender(unsigned _qpi)                                               = 0;
    virtual uint32_t getBackpressureOffset(uint16_t nic)                                   = 0;

    virtual hcclResult_t openQpsHLS(HCL_Comm comm);
    virtual hcclResult_t openQpsHlsScaleUp(HCL_Comm comm) = 0;

    virtual hcclResult_t openQps(HCL_Comm comm, const UniqueSortedVector& ranks);

    void checkSignals();

    IEventQueueHandler*  m_eqHandler        = nullptr;
    HclCommandsGen2Arch& m_commands;
    ScaleoutProvider*    m_scaleoutProvider = nullptr;
    EthStats             m_ethStats;

    virtual bool isNicUp(uint32_t nic) override;
    virtual hcclResult_t
    setupQps(HCL_Comm comm, HCL_Rank rank, uint32_t qpId, uint32_t port, uint32_t qpn, uint8_t qpSet) override;

private:
    virtual HclConfigType getConfigType() = 0;
    virtual hcclResult_t  openQpsLoopback(HCL_Comm comm) = 0;

    void synchronizeRemoteRanks(const HCL_Comm comm, const UniqueSortedVector& remoteRanks);

protected:
    void         initRemoteNicsLoopback(HCL_Comm comm);
    virtual void setEdmaEngineGroupSizes() = 0;
    uint64_t m_allocationRangeStart = -1;  // start of addresses returnable from synDeviceMalloc
    uint64_t m_allocationRangeEnd   = -1;
    std::map<HCL_Comm, std::map<std::pair<HCL_Rank, HCL_Rank>, nics_mask_t>> m_activeNicsSingleRankCache;

    std::unordered_map<HCL_Comm, std::set<HCL_Rank>> m_QpConnectionExistsForRank;

    Gen2ArchPortMappingConfig m_portMappingConfig;

    const unsigned m_cgSize;
};
