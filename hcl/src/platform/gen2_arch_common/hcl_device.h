#pragma once

#include <cstddef>        // for NULL
#include <cstdint>        // for uint32_t, uint8_t, uint64_t
#include <set>            // for set
#include <unordered_map>  // for unordered_map
#include <unordered_set>  // for unordered_set
#include <memory>         // for unique_ptr

#include "hl_logger/hllog_core.hpp"  // for logger

#include "platform/gen2_arch_common/eth_stats.hpp"  // EthStats
#include "hcl_types.h"
#include "interfaces/hcl_idevice.h"                       // for IHclDevice
#include "hcl_api_types.h"                                // for HCL_Comm, HCL_Rank
#include "hccl_types.h"                                   // for hcclResult_t
#include "hcl_config.h"                                   // for HclConfig
#include "platform/gen2_arch_common/hcl_device_config.h"  // for HclDeviceConfig
#include "types.h"
#include "platform/gen2_arch_common/scaleout_provider.h"
#include "platform/gen2_arch_common/qp_manager.h"
#include "platform/gen2_arch_common/server_connectivity_types.h"  // for DEFAULT_COMM_ID
#include "platform/gen2_arch_common/nics_events_handler_impl.h"   // for NicsEventHandler

class Gen2ArchDevicePortMapping;
class HclCommandsGen2Arch;
class HclDeviceControllerGen2Arch;
class IEventQueueHandler;
class DeviceSimbPoolManagerBase;
class QPManager;
class Gen2ArchServerConnectivity;
class Gen2ArchServerDef;
class SimbPoolContainerAllocator;

namespace hcl
{
class Gen2ArchScalManager;
};  // namespace hcl

class HclDeviceGen2Arch : public IHclDevice
{
public:
    // Tests only ctor
    HclDeviceGen2Arch(const bool                   testCtor,  // dummy param to distinguish from Runtime ctor
                      HclDeviceControllerGen2Arch& controller,
                      HclDeviceConfig&             deviceConfig,
                      Gen2ArchServerDef&           serverDef);
    // Runtime ctor
    HclDeviceGen2Arch(HclDeviceControllerGen2Arch& controller,
                      HclDeviceConfig&             deviceConfig,
                      Gen2ArchServerDef&           serverDef);
    virtual ~HclDeviceGen2Arch() noexcept(false);
    HclDeviceGen2Arch(const HclDeviceGen2Arch&)            = delete;
    HclDeviceGen2Arch& operator=(const HclDeviceGen2Arch&) = delete;

    virtual void setTraceMarker(const synStreamHandle stream_handle, uint32_t val) override;
    virtual nics_mask_t
    getActiveNics(HCL_Rank fromRank, HCL_Rank toRank, int physicalQueueOffset, HCL_Comm comm) override;

    virtual void invalidateCache(HCL_Comm comm);

    virtual nics_mask_t getAllPorts(const int deviceId, const nics_mask_t enabledExternalPortsMask) const;
    virtual bool        isScaleOutPort(const uint16_t port, const HCL_Comm comm = DEFAULT_COMM_ID) const override;

    virtual hcclResult_t onNewCommStart(HCL_Comm comm, uint32_t commSize, HclConfig& config) override;
    virtual hcclResult_t destroyComm(HCL_Comm comm, bool force = false) override;
    void                 deleteCommConnections(HCL_Comm comm);
    virtual void         destroy() override;

    virtual bool              isDramAddressValid(uint64_t addr) const override;
    hcl::Gen2ArchScalManager& getScalManager();
    virtual void              updateDisabledPorts() = 0;
    virtual void              deleteMigrationQPs(HCL_Comm comm);
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

    virtual uint32_t createQpnInLKD(HCL_Comm comm, const uint32_t port, const uint8_t qpId) override;

    void                       updateRankHasQp(const HCL_Comm comm, const HCL_Rank remoteRank);
    DeviceSimbPoolManagerBase& getDeviceSimbPoolManager(const uint32_t streamIndex);
    uint64_t                   getSIBBufferSize() const;
    virtual void               getLagInfo(const uint16_t nic, uint8_t& lagIdx, uint8_t& lastInLag, const HCL_Comm comm);
    virtual hcclResult_t       openQpsScaleOut(HCL_Comm comm, const UniqueSortedVector& outerRanks) = 0;
    virtual HclCommandsGen2Arch& getGen2ArchCommands();
    ScaleoutProvider*            getScaleOutProvider();
    const std::set<HCL_Rank>&    getOpenScaleOutRanks(const HCL_Comm comm);
    unsigned                     getEdmaEngineWorkDistributionSize();
    uint8_t                      getNumQpSets(bool isScaleOut, HCL_Comm comm, HCL_Rank remoteRank);

    virtual uint32_t getNicToQpOffset([[maybe_unused]] const uint32_t nic) { return 0; }

    Gen2ArchServerDef&       getServerDef() final { return m_serverDef; }
    const Gen2ArchServerDef& getServerDefConst() const final { return m_serverDef; }

    Gen2ArchServerConnectivity&       getServerConnectivity() { return m_serverConnectivity; }
    const Gen2ArchServerConnectivity& getServerConnectivity() const { return m_serverConnectivity; }

    virtual void destroyQp(HCL_Comm comm, uint32_t port, uint32_t qpn) override;
    void         dfa(hl_logger::LoggerSPtr logger);

    virtual bool isACcbHalfFullForDeviceBenchMark(const unsigned archStreamIdx) override;

    virtual uint64_t getDRAMSize() override;
    virtual uint64_t getDRAMBaseAddr() override;

    virtual void setGaudiDirect() override;

    std::unique_ptr<SimbPoolContainerAllocator> m_sibContainerManager;
    HclDeviceControllerGen2Arch&                m_deviceController;

    SignalsCalculator& getSignalsCalculator() const { return *m_signalsCalculator; }

    virtual bool      supportNicFaultTolerance() const { return false; }
    NicsEventHandler& getNicsFaultHandler() { return *m_nicsEventsHandler; }
    virtual void      updateMigrationQpsToRts(const CommIds& commIds);
    virtual void      createMigrationQps(const HCL_Comm commId, const uint16_t nicDown);

    void getAsyncError(const std::vector<HCL_HwModuleId>& remoteModuleIDs,
                       const HCL_Comm                     comm,
                       hcclResult_t*                      asyncError,
                       std::string&                       errMessage);

    unsigned     getCgSize() { return m_cgSize; };
    virtual void faultToleranceCommInit(const HCL_Comm comm) { UNUSED(comm); };

protected:
    virtual void
    addQPsToQPManagerDB(const HCL_Comm comm, const HCL_Rank remoteRank, const QpsVector& qps, const size_t nic) = 0;

    virtual uint32_t
    getQpi(const HCL_Comm comm, const uint8_t nic, const HCL_Rank remoteRank, uint32_t qpn, uint8_t qpSet) = 0;
    virtual bool     isSender(unsigned _qpi)                                                               = 0;
    virtual uint32_t getCollectiveQpi(const HCL_CollectiveOp collectiveOp, const bool isSend)              = 0;

    virtual hcclResult_t openQpsHLS(const HCL_Comm comm);
    virtual hcclResult_t openQpsHlsScaleUp(const HCL_Comm comm) = 0;

    void checkSignals();

    virtual bool         isNicUp(uint32_t nic) override;
    virtual hcclResult_t establishQpConnectionWithPeerQp(HCL_Comm comm,
                                                         HCL_Rank rank,
                                                         uint32_t qpId,
                                                         uint32_t port,
                                                         uint32_t qpn,
                                                         uint8_t  qpSet) override;

    void             initRemoteNicsLoopback(const HCL_Comm comm);
    virtual void     setEdmaEngineGroupSizes() = 0;
    virtual uint16_t getMaxNumScaleUpPortsPerConnection(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const final;
    virtual uint16_t getLogicalScaleoutPortNum(
        const uint16_t nic) const override;  // Translates scaleout physical nic to scaleout logical port number

    hcl::Gen2ArchScalManager&          m_scalManager;
    IEventQueueHandler*                m_eqHandler = nullptr;
    HclCommandsGen2Arch&               m_commands;
    ScaleoutProvider*                  m_scaleoutProvider = nullptr;
    EthStats                           m_ethStats;
    std::unique_ptr<SignalsCalculator> m_signalsCalculator;

    uint64_t m_allocationRangeStart = -1;  // start of addresses returnable from synDeviceMalloc
    uint64_t m_allocationRangeEnd   = -1;
    std::map<HCL_Comm, std::map<std::pair<HCL_Rank, HCL_Rank>, nics_mask_t>> m_activeNicsSingleRankCache;

    std::unordered_map<HCL_Comm, std::set<HCL_Rank>> m_QpConnectionExistsForRank;

    const unsigned m_cgSize;

    Gen2ArchServerDef&          m_serverDef;
    Gen2ArchServerConnectivity& m_serverConnectivity;

    std::unique_ptr<NicsEventHandler> m_nicsEventsHandler = nullptr;

    unsigned edmaEngineGroupSizes[2] = {};

private:
    virtual HclConfigType getConfigType()                = 0;
    virtual hcclResult_t  openQpsLoopback(HCL_Comm comm) = 0;
};
