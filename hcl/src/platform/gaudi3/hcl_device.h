#pragma once

#include <cstdint>  // for uint32_t, uint8_t
#include <set>      // for set
#include <memory>   // for unique_ptr

#include "hcl_api_types.h"  // for HCL_Comm, HCL_...
#include "hlthunk.h"        // for hlthunk_device...
#include "infra/scal/gen2_arch_common/scal_stream.h"
#include "platform/gen2_arch_common/hcl_device.h"  // for HclDeviceGen2Arch
#include "qp_manager_scaleout.h"
#include "qp_manager_scaleup.h"
#include "platform/gaudi3/gaudi3_nic.h"
#include "platform/gaudi3/hal.h"                              // for Gaudi3Hal
#include "platform/gen2_arch_common/hcl_device_config.h"      // for HclDeviceConfig
#include "interfaces/hcl_hal.h"                               // for HalPtr
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"  // for Gaudi3BaseServerConnectivity
#include "hcl_types.h"                                        // for NicLkdEventsEnum, eIbvNicPhysicalState

class Gen2ArchDevicePortMapping;
class Gen2ArchServerDef;

class HclDeviceGaudi3 : public HclDeviceGen2Arch
{
public:
    // For tests only
    HclDeviceGaudi3(HclDeviceControllerGen2Arch& controller,
                    const int                    moduleId,
                    HclDeviceConfig&             deviceConfig,
                    Gen2ArchServerDef&           serverDef);
    // Runtime ctor
    HclDeviceGaudi3(HclDeviceControllerGen2Arch& controller,
                    HclDeviceConfig&             deviceConfig,
                    hcl::HalPtr                  halShared,
                    Gen2ArchServerDef&           serverDef);
    virtual ~HclDeviceGaudi3()                         = default;
    HclDeviceGaudi3(const HclDeviceGaudi3&)            = delete;
    HclDeviceGaudi3& operator=(const HclDeviceGaudi3&) = delete;

    virtual hlthunk_device_name getDeviceName() override;

    virtual uint8_t  getPeerNic(const HCL_Rank rank, const HCL_Comm comm, const uint8_t port) override;
    virtual unsigned getSenderWqeTableSize() override;
    virtual unsigned getReceiverWqeTableSize() override;

    const Gaudi3BaseServerConnectivity& getServerConnectivityGaudi3() const
    {
        return reinterpret_cast<const Gaudi3BaseServerConnectivity&>(getServerConnectivity());
    }

    Gaudi3BaseServerConnectivity& getServerConnectivityGaudi3()
    {
        return reinterpret_cast<Gaudi3BaseServerConnectivity&>(getServerConnectivity());
    }

    virtual hcclResult_t connectCommQps(HCL_Comm comm) override;
    virtual void         updateDisabledPorts() override;
    virtual hcclResult_t openQpsScaleOut(HCL_Comm comm, const UniqueSortedVector& outerRanks) override;
    virtual void         configQps(const HCL_Comm comm);
    virtual void         openWQs() override;
    virtual void setDefaultScaleUpPortQPWithNicOffsets(hcl::ScalStream& stream, const HCL_Comm comm, const bool isSend);
    QPUsage      getBaseQpAndUsage(const HclDynamicCommunicator& dynamicComm,
                                   HCL_CollectiveOp              collectiveOp,
                                   bool                          isSend,
                                   bool                          isComplexCollective,
                                   bool                          isReductionInIMB,
                                   bool                          isHierarchical,
                                   uint64_t                      count,
                                   uint64_t                      cellCount,
                                   HclConfigType                 boxType,
                                   bool                          isScaleOut        = false,
                                   HCL_Rank                      remoteRank        = HCL_INVALID_RANK,
                                   uint8_t                       qpSet             = 0,
                                   const bool                    isReduction       = false,
                                   HCL_CollectiveOp              complexCollective = eHCLNoCollective,
                                   bool                          isRoot            = false);

    virtual spHclNic allocateNic(uint32_t nic, uint32_t max_qps) override
    {
        return std::make_shared<Gaudi3Nic>(
            this,
            nic,
            max_qps,
            isScaleOutPort((uint16_t)nic /*, HCL_Comm comm*/),
            getServerConnectivityGaudi3().getBackpressureOffset(nic /*, HCL_Comm comm*/));
    }

    Gaudi3Nic* getNic(uint32_t nic) { return (Gaudi3Nic*)m_hclNic[nic].get(); }

    uint32_t getNicToQpOffset(const uint32_t nic) override { return getNic(nic)->nic2QpOffset; }

    const hcl::Gaudi3Hal& getGaudi3Hal() const
    {
        return (const hcl::Gaudi3Hal&)(*(dynamic_cast<const hcl::Gaudi3Hal*>(m_hal.get())));
    }
    // The following functions might be called not from main thread
    virtual void handleScaleoutNicStatusChange(const uint16_t nic, const bool up);  // Physical scaleout port number
    virtual bool supportNicFaultTolerance() const override { return true; }
    virtual void createMigrationQps(const HCL_Comm commId, const uint16_t nicDown) override;

    virtual void deleteMigrationQPs(const HCL_Comm comm) override;
    virtual void updateMigrationQpsToRts(const HCL_Comm comm) override;

    virtual void updateNicState(const uint32_t nic, const NicLkdEventsEnum event, const bool atInit) override;

protected:
    using HclDeviceGen2Arch::createQpnInLKD;  // to avoid compiler "hides overloaded virtual function" error
    uint32_t                           createQpnInLKD(const uint32_t nic, const unsigned qpId, uint32_t coll_qpn);
    uint32_t                           requestCollectiveQpnFromLKD(bool isScaleOut);
    virtual bool                       isSender(unsigned qpi) override;
    virtual const eIbvNicPhysicalState getNicPhysicalState(const uint32_t nic) override;

private:
    void          setInitialQpConfiguration(const HCL_Comm comm, const bool isSend);
    void          setEdmaEngineGroupSizes() override;
    HclConfigType getConfigType() override { return m_boxConfigType; }
    virtual void  getLagInfo(const uint16_t nic, uint8_t& lagIdx, uint8_t& lastInLag, const HCL_Comm comm) override;
    virtual void  addQPsToQPManagerDB(const HCL_Comm   comm,
                                      const HCL_Rank   remoteRank,
                                      const QpsVector& qps,
                                      const size_t     nic) override;

    virtual uint32_t     getQpi(HCL_Comm comm, uint8_t nic, HCL_Rank remoteRank, uint32_t qpn, uint8_t qpSet) override;
    virtual hcclResult_t openQpsHlsScaleUp(HCL_Comm comm) override;
    virtual hcclResult_t openQpsLoopback(HCL_Comm comm) override;
    void reserveRankQps(const HCL_Comm comm, const bool isScaleOut, const HCL_Rank remoteRank, QpsVector& qpnArr);
    void createRankQps(HCL_Comm comm, HCL_Rank rank, nics_mask_t nics, QpsVector& qpnArr, const bool isScaleOut);
    void createRankQpsLoopback(HCL_Comm comm, HCL_Rank rank, QpsVector& qpnArr);
    void createNicQps(HCL_Comm comm, HCL_Rank rank, uint8_t nic, QpsVector& qpnArr, uint8_t qpSets);

    void openScaleOutMigrationQps(const HCL_Comm comm, const uint16_t fromPort, const uint16_t toPort);
    void reportCommNicStatus(const uint16_t port, const bool up);
    void setMigrationQPsRTR(const HCL_Comm comm);
    void setMigrationQPsRTS(const HCL_Comm comm);
    void migrateQPs(const HCL_Comm comm);

    HclConfigType m_boxConfigType = HLS3;

    std::unique_ptr<MigrationScaleOutQpManagerGaudi3> m_migrationQpManager;
};
