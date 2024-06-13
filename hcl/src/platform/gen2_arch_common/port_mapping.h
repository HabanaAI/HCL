#pragma once

#include <array>          // for array
#include <cstdint>        // for uint8_t
#include <map>            // for map
#include <tuple>          // for tuple
#include <utility>        // for pair
#include <vector>         // for vector
#include <unordered_map>  // for unordered_map

#include "platform/gen2_arch_common/types.h"                // for MAX_NICS_GEN2ARCH, GEN2ARCH_HLS_BOX_SIZE
#include "platform/gen2_arch_common/port_mapping_config.h"  // for Gen2ArchPortMappingConfig
#include "hcl_types.h"                                      // for NUM_SCALEUP_PORTS_PER_CONNECTION

typedef std::array<Gen2ArchNicsDeviceSingleConfig, GEN2ARCH_HLS_BOX_SIZE> ServerNicsConnectivityArray;

class Gen2ArchDevicePortMapping
{
public:
    Gen2ArchDevicePortMapping(int fd);
    Gen2ArchDevicePortMapping(const int fd, const int moduleId);  // for testing
    virtual ~Gen2ArchDevicePortMapping() = default;

    virtual void                   assignDefaultMapping() = 0;
    virtual void                   assignCustomMapping(int fd, const Gen2ArchPortMappingConfig& portMappingConfig) = 0;
    virtual int                    getRemoteDevice(int port, unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual int                    getPeerPort(int port, unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual int                    getSubPortIndex(int port, unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual bool isScaleoutPort(const unsigned port, const unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual int                    getMaxSubPort(unsigned spotlightType) const;
    virtual uint64_t               getEnabledPortsMask() const;
    virtual uint64_t               getExternalPortsMask() const;
    virtual void                   verifyPortsConfiguration(unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual nics_mask_t            getAllPorts(int deviceId, unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual void                   setNumScaleUpPorts();
    virtual unsigned               getNumScaleUpPorts(const unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual void                   setNumScaleOutPorts();
    virtual unsigned               getNumScaleOutPorts(unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual nics_mask_t            getScaleOutPorts(unsigned spotlightType = DEFAULT_SPOTLIGHT) const;
    virtual unsigned               getScaleoutSubPortIndex(unsigned port, unsigned spotlightType = DEFAULT_SPOTLIGHT);
    virtual void                   setUpateScaleOutGlobalContextRequired(uint64_t lkd_mask);
    virtual bool                   isUpateScaleOutGlobalContextRequired();
    virtual void                   setMaxNumScaleOutPorts(uint64_t lkd_mask);
    virtual unsigned               getMaxNumScaleOutPorts() const              = 0;
    virtual unsigned               getDefaultScaleOutPortByIndex(unsigned idx) const = 0;

protected:
    Gen2ArchNicsSpotlightBoxConfigs m_spotlight_mappings;
    nics_mask_t                     m_enabled_ports_mask          = 0;
    nics_mask_t                     m_enabled_external_ports_mask = 0;
    nics_mask_t                     lkd_enabled_scaleout_ports;
    unsigned                        m_max_scaleout_ports;
    int                             m_moduleId = -1;

private:
    int                                                                  m_fd;
    std::array<nics_mask_t, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH> m_enabled_scaleup_ports;
    std::array<nics_mask_t, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH> m_enabled_scaleout_ports;
    std::array<std::unordered_map<unsigned int, unsigned int>, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH>
         m_enabled_scaleout_sub_ports;
    bool m_upateScaleOutGlobalContextRequired;
};
