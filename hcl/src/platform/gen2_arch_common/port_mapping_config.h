#pragma once

#include <array>    // for array
#include <cstdint>  // for uint8_t
#include <tuple>    // for tuple
#include <nlohmann/json.hpp>  // for json

#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH, GEN2ARCH_HLS_BOX_SIZE
#include "hcl_types.h"

using json = nlohmannV340::json;

// <remote card location, remote nic, sub nic index>
using Gen2ArchNicDescriptor = std::tuple<unsigned, uint8_t, uint8_t>;  // remote device id (-1 for SO), remote nic in
                                                                       // device, remote sub-nic index (0-2)
typedef std::array<Gen2ArchNicDescriptor, MAX_NICS_GEN2ARCH>
    Gen2ArchNicsDeviceSingleConfig;  // array of remote nics per current device nics
typedef std::array<Gen2ArchNicsDeviceSingleConfig, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH>
    Gen2ArchNicsDeviceConfig;  // array of spotlight configurations of arrays of remote nics per current device nics
typedef std::array<Gen2ArchNicsDeviceSingleConfig, GEN2ARCH_HLS_BOX_SIZE>
    Gen2ArchNicsBoxConfig;  // array for all devices nics configs
typedef std::array<Gen2ArchNicsBoxConfig, MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH> Gen2ArchNicsSpotlightBoxConfigs;

constexpr unsigned SCALEOUT_DEVICE_ID = -1;
constexpr unsigned NOT_CONNECTED_DEVICE_ID = -2;
constexpr unsigned MAX_SUB_NICS            = 6;  // TODO: per server type

class Gen2ArchPortMappingConfig
{
public:
    Gen2ArchPortMappingConfig()          = default;
    virtual ~Gen2ArchPortMappingConfig() = default;

    /**
     * @brief Tries to read input json file path and parse port mapping form it
     *
     * @return true if provided file is valid and parsed
     * @return false otherwise
     */
    bool parseConfig(const std::string path);

    /**
     * @brief Accesses the port mapping configuration read from file, can only be read if it was valid
     *
     * @return The port mapping configuration read from file
     */
    const Gen2ArchNicsBoxConfig& getMapping() const;

    /**
     * @brief Several types of spotlight configuration are supported. Custom JSON configuration from the user should
     * override the requested spotlight type.
     *
     * @return The spotlight type
     */
    const unsigned getSpotlightType() const;

    /**
     * @return If the json mapping file read was valid or not
     */
    bool hasValidMapping() const { return m_hasValidMapping; }

    /**
     * @return The name of the json file read, if it was valid
     */
    const std::string& getFilePathLoaded() const { return m_filePathLoaded; }

private:
    virtual bool parseNics(const std::string& path, const json& config);

    bool                  m_hasValidMapping = false;
    std::string           m_filePathLoaded;
    Gen2ArchNicsBoxConfig m_customMapping;
    unsigned              m_spotlightType = DEFAULT_SPOTLIGHT;
};

/**
 * @brief Logs the mapping data structure to log file
 *
 */
void logPortMappingConfig(const Gen2ArchNicsBoxConfig& mapping);
