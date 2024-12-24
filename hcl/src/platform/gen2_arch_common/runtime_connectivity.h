#pragma once

#include <array>          // for array
#include <cstdint>        // for uint*_t
#include <map>            // for map
#include <tuple>          // for tuple
#include <utility>        // for pair
#include <unordered_map>  // for unordered_map

#include "hcl_api_types.h"                                              // for HCL_Comm
#include "platform/gen2_arch_common/server_connectivity_user_config.h"  // for ServerConnectivityUserConfig
#include "platform/gen2_arch_common/server_connectivity_types.h"        // for ServerNicsConnectivityArray

#include "hcl_bits.h"  // for nics_mask_t

class Gen2ArchServerConnectivity;
class HclDynamicCommunicator;

//
// Configuration per comm
//
class Gen2ArchRuntimeConnectivity
{
public:
    Gen2ArchRuntimeConnectivity(const int                   moduleId,
                                const HCL_Comm              hclCommId,
                                Gen2ArchServerConnectivity& serverConnectivity);
    virtual ~Gen2ArchRuntimeConnectivity()                                     = default;
    Gen2ArchRuntimeConnectivity(const Gen2ArchRuntimeConnectivity&)            = delete;
    Gen2ArchRuntimeConnectivity& operator=(const Gen2ArchRuntimeConnectivity&) = delete;

    virtual void init(const ServerNicsConnectivityArray&  serverNicsConnectivityArray,
                      const ServerConnectivityUserConfig& usersConnectivityConfig,
                      const bool                          readLkdPortsMask);  // can be overriden for unit tests

    virtual void onCommInit(HclDynamicCommunicator& dynamicComm) const {};
    int          getRemoteDevice(const uint16_t port) const;
    uint16_t     getPeerPort(const uint16_t port) const;
    uint16_t     getSubPortIndex(const uint16_t port) const;
    uint16_t     getScaleoutNicFromSubPort(const uint16_t subPort) const;
    bool         isScaleoutPort(const uint16_t port) const;
    uint16_t     getMaxSubPort(const bool isScaleoutPort) const;
    uint64_t     getEnabledPortsMask() const;
    uint16_t     getDefaultScaleUpPort() const;
    uint64_t     getExternalPortsMaskGlbl() const;           // Includes LKD & HCL Masks
    nics_mask_t  getAllPortsGlbl(const int deviceId) const;  // All scaleup ports to device, after LKD mask
    nics_mask_t  getAllPorts(const int deviceId, const nics_mask_t enabledExternalPortsMask) const;
    uint16_t     getNumScaleUpPorts() const;       // LKD Mask Irrelevant
    uint16_t     getNumScaleOutPortsGlbl() const;  // Includes LKD & HCL Masks
    nics_mask_t  getScaleOutPortsGlbl() const;     // Includes LKD & HCL Masks
    nics_mask_t  getScaleUpPorts() const;
    uint16_t     getScaleoutSubPortIndexGlbl(const uint16_t port) const;
    bool         isUpdateScaleOutGlobalContextRequired() const { return m_updateScaleOutGlobalContextRequiredGlbl; };
    const nics_mask_t getAllScaleoutPorts() const { return m_fullScaleoutPorts; }  // Used for unit tests
    virtual uint32_t  getBackpressureOffset(const uint16_t nic) const = 0;
    virtual uint16_t  getMaxNumScaleUpPortsPerConnection() const      = 0;

private:
    void setPortsMasksGlbl();
    void setNumScaleOutPortsGlbl();

protected:
    bool         isPortConnected(const uint16_t port) const;
    virtual void initServerSpecifics() = 0;

    const int                   m_moduleId;
    const HCL_Comm              m_hclCommId;  // This instance comm id
    Gen2ArchServerConnectivity& m_serverConnectivity;

    // Vars per comm
    ServerNicsConnectivityArray            m_mappings;
    nics_mask_t                            m_enabledExternalPortsMaskGlbl {};  // After masking by LKD & HCL
    uint16_t                               m_maxSubNicScaleup  = 0;            // w/o any masks
    uint16_t                               m_maxSubNicScaleout = 0;            // w/o any masks
    nics_mask_t                            m_enabledScaleupPorts;              // w/o any masks
    nics_mask_t                            m_enabledScaleoutPortsGlbl;         // After LKD, HCL Mask
    std::unordered_map<uint16_t, uint16_t> m_enabledScaleoutSubPortsGlbl;  // Key => Port, Value => max sub port index
    bool                                   m_updateScaleOutGlobalContextRequiredGlbl = false;
    nics_mask_t m_fullScaleoutPorts;  // All possible scaleout ports regardless of the LKD/User masks
    nics_mask_t m_allPorts;           // All possible connected ports regardless of the LKD/User masks

private:
    /**
     * @brief Logs the mapping data structure to log file
     *
     */
    void logPortMappingConfig(const ServerNicsConnectivityArray& mapping);
    void assignDefaultMapping(const ServerNicsConnectivityArray& serverNicsConnectivityArray);
    void assignCustomMapping(const ServerConnectivityUserConfig& usersConnectivityConfig);
    void readAllPorts();  // From ports map configuration, regardless of masks
    void verifyPortsConfiguration() const;
    void setNumScaleUpPorts();
    void setMaxSubNics();
    void setUpdateScaleOutGlobalContextRequiredGlbl(const nics_mask_t lkd_mask,
                                                    const nics_mask_t scaleOutPortsMask);  // relevant for HLS2 only
};

namespace RuntimePortsMasksUtils
{
struct SetPortsMaskInput
{
    const HCL_Comm                    comm;
    const Gen2ArchServerConnectivity& serverConnectivity;
    const nics_mask_t                 operationalScaleOutPortsMask;
    const int                         moduleId;
};

struct SetPortsMaskOutput
{
    nics_mask_t enabledExternalPortsMask;
    uint64_t    scaleOutPortsMask = 0;
};
SetPortsMaskOutput setPortsMasksCommon(const SetPortsMaskInput input);
}  // namespace RuntimePortsMasksUtils
