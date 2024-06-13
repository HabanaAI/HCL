#pragma once

#include <cstdint>                                      // for uint32_t, uint8_t
#include <set>                                          // for set
#include <memory>                                       // for unique_ptr

#include "hcl_api_types.h"                              // for HCL_Comm, HCL_...
#include "hlthunk.h"                                    // for hlthunk_device...
#include "infra/scal/gen2_arch_common/scal_stream.h"
#include "platform/gaudi3/port_mapping.h"               // for Gaudi3DevicePortMapping
#include "platform/gen2_arch_common/hcl_device.h"       // for HclDeviceGen2Arch
#include "qp_manager.h"                                 // for QPManager
#include "platform/gaudi3/gaudi3_nic.h"
#include "platform/gaudi3/hal.h"  // for Gaudi3Hal

class Gen2ArchDevicePortMapping;
class HclDeviceConfig;
namespace hcl
{
class Gen2ArchScalManager;
}

class HclDeviceGaudi3 : public HclDeviceGen2Arch
{
public:
    HclDeviceGaudi3(HclDeviceControllerGen2Arch& controller);                      // for test only
    HclDeviceGaudi3(HclDeviceControllerGen2Arch& controller, const int moduleId);  // for tests only
    HclDeviceGaudi3(HclDeviceControllerGen2Arch& controller, HclDeviceConfig& deviceConfig);
    virtual ~HclDeviceGaudi3() = default;

    virtual hlthunk_device_name getDeviceName() override;

    virtual uint8_t                  getPeerNic(HCL_Rank rank, HCL_Comm comm, uint8_t port) override;
    virtual unsigned                 getSenderWqeTableSize() override;
    virtual unsigned                 getReceiverWqeTableSize() override;
    virtual uint32_t                 getBackpressureOffset(uint16_t nic) override;
    const Gen2ArchDevicePortMapping& getPortMapping() override { return *m_portMapping; };
    virtual Gaudi3DevicePortMapping& getPortMappingGaudi3() { return *m_portMapping; };
    virtual bool                     isScaleOutPort(uint16_t port, unsigned spotlightType) override;
    virtual hcclResult_t             updateQps(HCL_Comm comm) override;
    virtual void                     updateDisabledPorts() override;
    void                             deleteCommConnections(HCL_Comm comm) override;
    virtual uint64_t                 getEnabledPortsMask() override;
    virtual nics_mask_t              getAllPorts(int deviceId, unsigned spotlightType) override;
    virtual hcclResult_t             openQpsHlsScaleOut(HCL_Comm comm, const UniqueSortedVector& outerRanks) override;
    virtual void                     openWQs() override;

    std::unique_ptr<QPManagerScaleUp>  m_qpManagerScaleUp  = nullptr;  // Needs late init in ctor after Hal
    std::unique_ptr<QPManagerScaleOut> m_qpManagerScaleOut = nullptr;  // Needs late init in ctor after Hal

    virtual spHclNic allocateNic(uint32_t nic, uint32_t max_qps) override
    {
        if (GCFG_HCL_USE_IBVERBS.value())
        {
            return std::make_shared<Gaudi3IBVNic>(this, nic, max_qps, isScaleOutPort(nic, SCALEOUT_SPOTLIGHT), getBackpressureOffset(nic));
        };

        return std::make_shared<Gaudi3Nic>(this, nic, max_qps, getBackpressureOffset(nic));
    }

    Gaudi3Nic* getNic(uint32_t nic) { return (Gaudi3Nic*)m_hclNic[nic].get(); }
    Gaudi3IBVNic* getIBVNic(uint32_t nic) { return (Gaudi3IBVNic*)m_hclNic[nic].get(); }

    uint32_t getNicToQpOffset(uint32_t nic)
    {
        if (GCFG_HCL_USE_IBVERBS.value())
        {
            return getIBVNic(nic)->nic2QpOffset;
        }

        return getNic(nic)->nic2QpOffset;
    }

protected:
    uint32_t     createQp(uint32_t nic, unsigned qpId, uint32_t coll_qpn);
    uint32_t     createCollectiveQp(bool isScaleOut);
    uint32_t     getDestQpi(unsigned qpi) override;
    virtual bool isSender(unsigned qpi) override;
    const hcl::Gaudi3Hal& getGaudi3Hal() const
    {
        return (const hcl::Gaudi3Hal&)(*(dynamic_cast<const hcl::Gaudi3Hal*>(m_hal.get())));
    }

private:
    std::unique_ptr<Gaudi3DevicePortMapping> m_portMapping   = nullptr;  // Needs late init in ctor after Hal
    HclConfigType                            m_boxConfigType = HLS3;

    void          setEdmaEngineGroupSizes() override;
    HclConfigType getConfigType() override { return m_boxConfigType; }
    virtual void  getLagInfo(int nic, uint8_t& lagIdx, uint8_t& lastInLag, unsigned spotlightType) override;
    virtual void  registerQps(HCL_Comm comm, HCL_Rank remoteRank, const QpsVector& qps, int nic = INVALID_NIC) override;

    virtual uint32_t     getQpi(HCL_Comm comm, uint8_t nic, HCL_Rank remoteRank, uint32_t qpn, uint8_t qpSet) override;
    virtual hcclResult_t openQpsHlsScaleUp(HCL_Comm comm) override;
    virtual hcclResult_t openQpsLoopback(HCL_Comm comm) override;
    void allocateQps(const HCL_Comm comm, const bool isScaleOut, const HCL_Rank remoteRank, QpsVector& qpnArr);
    void openRankQps(HCL_Comm comm, HCL_Rank rank, nics_mask_t nics, QpsVector& qpnArr, const bool isScaleOut);
    void openRankQpsLoopback(HCL_Comm comm, QpsVector& qpnArr);
    void createNicQps(HCL_Comm comm, HCL_Rank rank, uint8_t nic, QpsVector& qpnArr, uint8_t qpSets);
};
