#pragma once

#include "platform/gen2_arch_common/hcl_device.h"  // for HclDeviceGen2Arch

#include <cstddef>        // for NULL
#include <cstdint>        // for uint32_t, uint8_t
#include <map>            // for map
#include <set>            // for set
#include <unordered_set>  // for unordered_set
#include <utility>        // for pair
#include <unordered_map>  // for unordered_map

#include "hcl_global_conf.h"                           // for GCFG_* - hcl.so
#include "hcl_api_types.h"                             // for HCL_Comm, HCL_...
#include "hlthunk.h"                                   // for hlthunk_device...
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for Gen2ArchScalMa...
#include "gaudi2_nic.h"
#include "qp_manager_scaleout.h"
#include "qp_manager_scaleup.h"
#include "platform/gen2_arch_common/hcl_device_config.h"  // for HclDeviceConfig
#include "interfaces/hcl_hal.h"                           // for HalPtr
#include "platform/gaudi2/context_manager.h"              // for Context...

class Gen2ArchDevicePortMapping;
class HclDeviceControllerGen2Arch;
class Gen2ArchServerDef;

class HclDeviceGaudi2 : public HclDeviceGen2Arch
{
public:
    // Tests only ctor
    HclDeviceGaudi2(HclDeviceControllerGen2Arch& controller,
                    HclDeviceConfig&             deviceConfig,
                    Gen2ArchServerDef&           serverDef);
    // Runtime ctor
    HclDeviceGaudi2(HclDeviceControllerGen2Arch& controller,
                    HclDeviceConfig&             deviceConfig,
                    hcl::HalPtr                  halShared,
                    Gen2ArchServerDef&           serverDef);
    virtual ~HclDeviceGaudi2()                         = default;
    HclDeviceGaudi2(const HclDeviceGaudi2&)            = delete;
    HclDeviceGaudi2& operator=(const HclDeviceGaudi2&) = delete;

    virtual hlthunk_device_name getDeviceName() override;

    ContextManager& getContextManager();

    virtual uint8_t  getPeerNic(const HCL_Rank rank, const HCL_Comm comm, const uint8_t port) override;
    virtual unsigned getSenderWqeTableSize() override;
    virtual unsigned getReceiverWqeTableSize() override;

    virtual hcclResult_t openQpsScaleOut(HCL_Comm comm, const UniqueSortedVector& outerRanks) override;
    hcclResult_t         connectCommQps(HCL_Comm comm) override;

    void openQpToSingleRank(const HCL_Comm comm, const HCL_Rank remoteRank);

    virtual void updateDisabledPorts() override;

    virtual spHclNic allocateNic(uint32_t nic, uint32_t max_qps) override;

protected:
    std::unique_ptr<ContextManager> m_contextManager;

private:
    hcclResult_t  openQpToRemoteRanks(const HCL_Comm comm, const UniqueSortedVector& ranks);
    void          setEdmaEngineGroupSizes() override;
    HclConfigType getConfigType() override { return m_boxConfigType; };

    virtual void     addQPsToQPManagerDB(const HCL_Comm   comm,
                                         const HCL_Rank   remoteRank,
                                         const QpsVector& qps,
                                         const size_t     nic) override;
    virtual bool     isSender(unsigned qpi) override;
    virtual uint32_t getQpi(HCL_Comm comm, uint8_t nic, HCL_Rank remoteRank, uint32_t qpn, uint8_t qpSet) override;

    virtual hcclResult_t openQpsHlsScaleUp(HCL_Comm comm) override;
    virtual hcclResult_t openQpsLoopback(HCL_Comm comm) override;

    HclConfigType m_boxConfigType = HLS2;
};
