#pragma once

#include <array>              // for array
#include <cstdint>            // for uint*_t
#include <tuple>              // for tuple
#include <nlohmann/json.hpp>  // for json

#include "platform/gen2_arch_common/server_connectivity_types.h"  // for ServerNicsConnectivityArray

using json = nlohmannV340::json;

class ServerConnectivityUserConfig
{
public:
    ServerConnectivityUserConfig()          = default;
    virtual ~ServerConnectivityUserConfig() = default;

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
    const ServerNicsConnectivityArray& getMapping() const { return m_customMapping; };

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

    bool                        m_hasValidMapping = false;
    std::string                 m_filePathLoaded;
    ServerNicsConnectivityArray m_customMapping;
};
