#pragma once

#include <array>    // for array
#include <vector>   // for vector
#include <cstdint>  // for uint*_t
#include <memory>   // for unique_ptr

#include "platform/gen2_arch_common/server_connectivity_user_config.h"  // for ServerConnectivityUserConfig
#include "platform/gen2_arch_common/server_connectivity_types.h"        // for ServerNicsConnectivityArray
#include "platform/gen2_arch_common/runtime_connectivity.h"             // for Gen2ArchRuntimeConnectivity
#include "hcl_bits.h"                                                   // for nics_mask_t
#include "hcl_types.h"                                                  // for portMaskConfig

// forward decl
class HclDynamicCommunicator;
class HclDeviceConfig;

using Gen2ArchRuntimeConnectivityPtr = std::unique_ptr<Gen2ArchRuntimeConnectivity>;

static constexpr unsigned INVALID_PORTS_MASK = (unsigned)-1;

extern const ServerNicsConnectivityArray g_dummyTestDeviceServerNicsConnectivity;

class Gen2ArchServerConnectivity
{
public:
    Gen2ArchServerConnectivity(const int                          fd,
                               const int                          moduleId,
                               const bool                         useDummyConnectivity,
                               const ServerNicsConnectivityArray& serverNicsConnectivityArray,
                               HclDeviceConfig&                   deviceConfig);
    virtual ~Gen2ArchServerConnectivity()                                    = default;
    Gen2ArchServerConnectivity(const Gen2ArchServerConnectivity&)            = delete;
    Gen2ArchServerConnectivity& operator=(const Gen2ArchServerConnectivity&) = delete;

    virtual void init(const bool readLkdPortsMask);
    virtual void onCommInit(HclDynamicCommunicator& dynamicComm) {};  // Default implementation for G2 do nothing

    int         getModuleId() const { return m_moduleId; }
    int         getRemoteDevice(const uint16_t port, const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    uint16_t    getPeerPort(const uint16_t port, const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    uint16_t    getSubPortIndex(const uint16_t port, const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    uint16_t    getScaleoutNicFromSubPort(const uint16_t subPort, const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    bool        isScaleoutPort(const uint16_t port, const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    uint16_t    getMaxSubPort(const bool     isScaleoutPort,
                              const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;             // No mask
    nics_mask_t getEnabledPortsMask() const { return m_enabledPortsMask; }                   // After mask by LKD only
    uint16_t    getDefaultScaleUpPort(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;     // No masks
    uint64_t    getExternalPortsMaskGlbl(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;  // Includes LKD & HCL Masks
    nics_mask_t
                getAllPortsGlbl(const int      deviceId,
                                const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;  // All scaleup ports to device, after LKD mask
    nics_mask_t getAllPorts(const int deviceId, const nics_mask_t enabledExternalPortsMask) const;
    uint16_t    getNumScaleUpPorts(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;       // LKD Mask irrelevant
    uint16_t    getNumScaleOutPortsGlbl(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;  // Includes LKD & HCL Masks
    nics_mask_t getScaleOutPortsGlbl(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;     // Includes LKD & HCL Masks
    nics_mask_t getScaleUpPorts(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    uint16_t    getScaleoutSubPortIndexGlbl(const uint16_t port,
                                            const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;  // Includes LKD & HCL Masks
    bool        isUpdateScaleOutGlobalContextRequired(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    uint16_t    getMaxNumScaleOutPorts() const { return m_maxScaleoutPorts; };   // Includes LKD mask only
    uint16_t    getDefaultScaleOutPortByIndex(const uint16_t nicIdx = 0) const;  // Includes LKD mask only
    uint64_t    getUserScaleOutPortsMask() const { return m_userScaleOutPortsMask; };
    nics_mask_t getLkdEnabledScaleoutPorts() const { return m_lkdEnabledScaleoutPorts; };
    HclDeviceConfig&       getDeviceConfig() { return m_deviceConfig; }
    const HclDeviceConfig& getDeviceConfig() const { return m_deviceConfig; }
    uint32_t               getBackpressureOffset(const uint16_t nic, const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    const nics_mask_t
    getAllScaleoutPorts(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;  // Used for unit tests, w/o any masks
    uint16_t getMaxNumScaleUpPortsPerConnection(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;

    void setUnitTestsPortsMasks(const nics_mask_t fullScaleoutPorts, const nics_mask_t allPortsMask);

protected:
    virtual Gen2ArchRuntimeConnectivity*
    createRuntimeConnectivityFactory(const int                   moduleId,
                                     const HCL_Comm              hclCommId,
                                     Gen2ArchServerConnectivity& serverConnectivity) = 0;

    const int  m_fd       = UNIT_TESTS_FD;        // This device FD, can stay -1 for unit tests
    const int  m_moduleId = UNDEFINED_MODULE_ID;  // This device module id, can stay -1 for unit tests
    const bool m_useDummyConnectivity;
    const ServerNicsConnectivityArray& m_serverNicsConnectivityArray;  // Init this from all sub-classes
    HclDeviceConfig&                   m_deviceConfig;
    struct portMaskConfig              m_lkdPortsMasks;  // Stores LKD ports mask
    bool                               m_lkdPortsMaskValid = false;
    uint64_t m_userScaleOutPortsMask = INVALID_PORTS_MASK;  // Stores users's external ports mask if supplied

    std::vector<Gen2ArchRuntimeConnectivityPtr>
                m_commsRuntimeConnectivity;  // vector of dynamic runtime connectivity per comm
    nics_mask_t m_enabledPortsMask = nics_mask_t(INVALID_PORTS_MASK);  // After mask by LKD only, includes scaleup
    nics_mask_t m_lkdEnabledScaleoutPorts;                             // After masking by LKD only
    uint16_t    m_maxScaleoutPorts;                                    // After masking by LKD only

private:
    virtual void                 readDeviceLkdPortsMask();
    ServerConnectivityUserConfig m_usersConnectivityConfig;
};
