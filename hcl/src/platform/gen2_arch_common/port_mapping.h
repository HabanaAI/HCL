#pragma once

#include <array>          // for array
#include <cstdint>        // for uint8_t
#include <map>            // for map
#include <tuple>          // for tuple
#include <utility>        // for pair
#include <vector>         // for vector
#include <unordered_map>  // for unordered_map

#include "hcl_dynamic_communicator.h"
#include "platform/gen2_arch_common/types.h"                // for MAX_NICS_GEN2ARCH, GEN2ARCH_HLS_BOX_SIZE
#include "platform/gen2_arch_common/port_mapping_config.h"  // for Gen2ArchPortMappingConfig

typedef std::array<Gen2ArchNicsDeviceSingleConfig, GEN2ARCH_HLS_BOX_SIZE> ServerNicsConnectivityArray;

class Gen2ArchDevicePortMapping
{
public:
    Gen2ArchDevicePortMapping(int fd);
    Gen2ArchDevicePortMapping(const int fd, const int moduleId);  // for testing
    virtual ~Gen2ArchDevicePortMapping() = default;

    virtual void        setPortsMasks();
    virtual void        onCommInit(HclDynamicCommunicator& dynamicComm) {};
    virtual void        assignDefaultMapping()                                                          = 0;
    virtual void        assignCustomMapping(const Gen2ArchPortMappingConfig& portMappingConfig)         = 0;
    virtual int         getRemoteDevice(int port, unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual int         getPeerPort(int port, unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual int         getSubPortIndex(int port, unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual int         getScaleoutNicFromSubPort(const int subPort, const unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual bool        isScaleoutPort(const unsigned port, const unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual bool        isPortConnected(const uint16_t port, const unsigned spotlightType) const;
    virtual int         getMaxSubPort(const bool isScaleoutPort, const unsigned spotlightType) const;
    virtual uint64_t    getEnabledPortsMask() const;
    virtual uint64_t    getExternalPortsMask() const;
    virtual void        verifyPortsConfiguration(unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual nics_mask_t getAllPorts(int deviceId, unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual void        setNumScaleUpPorts();
    virtual unsigned    getNumScaleUpPorts(const unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual void        readMaxScaleOutPorts();  // From ports map configuration, regardless of masks
    virtual void        setNumScaleOutPorts();
    virtual void        setMaxSubNics();
    virtual unsigned    getNumScaleOutPorts(unsigned spotlightType = DEFAULT_SPOTLIGHT) const;  // Includes LKD & HCL Masks
    virtual nics_mask_t getScaleOutPorts(unsigned spotlightType = DEFAULT_SPOTLIGHT) const;  // Includes LKD & HCL Masks
    virtual unsigned    getScaleoutSubPortIndex(unsigned port, unsigned spotlightType = DEFAULT_SPOTLIGHT);
    virtual void        setUpateScaleOutGlobalContextRequired(const uint64_t lkd_mask, const uint64_t scaleOutPortsMask);
    virtual bool        isUpateScaleOutGlobalContextRequired();
    virtual void        setMaxNumScaleOutPorts(const uint64_t lkd_mask);                  // Stores LKD mask
    unsigned            getMaxNumScaleOutPorts() const { return m_max_scaleout_ports; };  // Includes LKD mask only
    virtual unsigned    getDefaultScaleOutPortByIndex(unsigned idx) const = 0;            // Includes LKD mask only

protected:
    Gen2ArchNicsSpotlightBoxConfigs                    m_spotlight_mappings;
    nics_mask_t                                        m_enabled_ports_mask          = 0;
    nics_mask_t                                        m_enabled_external_ports_mask = 0;  // After masking by LKD & HCL
    nics_mask_t                                        m_lkd_enabled_scaleout_ports;       // After masking by LKD only
    unsigned                                           m_max_scaleout_ports;               // After masking by LKD only
    int                                                m_moduleId          = -1;
    std::array<int, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH> m_maxSubNicScaleup  = {-1};
    std::array<int, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH> m_maxSubNicScaleout = {-1};

private:
    int                                                                  m_fd;
    std::array<nics_mask_t, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH> m_enabled_scaleup_ports;
    std::array<nics_mask_t, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH> m_enabled_scaleout_ports;
    std::array<std::unordered_map<unsigned int, unsigned int>, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH>
         m_enabled_scaleout_sub_ports;
    bool m_upateScaleOutGlobalContextRequired;
    std::array<nics_mask_t, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH>
        m_fullScaleoutPorts;  // All possible scaleout ports regardless of the LKD/User masks
};
