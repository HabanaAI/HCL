#include "platform/gen2_arch_common/server_connectivity_user_config.h"

#include <cstdint>        // for uint*_t
#include <unordered_set>  // for unordered_set
#include <fstream>        // for ifstream

#include <nlohmann/json.hpp>  // for json

#include "platform/gen2_arch_common/types.h"                      // for MAX_NICS_GEN2ARCH
#include "platform/gen2_arch_common/server_connectivity_types.h"  // for ServerNicsConnectivityVector
#include "hcl_utils.h"                                            // for LOG_HCL_*
#include "hcl_log_manager.h"                                      // for LOG_*

using json = nlohmann::json;

ServerConnectivityUserConfig::ServerConnectivityUserConfig(const uint32_t numberOfDevicesPerHost)
: m_numberOfDevicesPerHost(numberOfDevicesPerHost)
{
    m_customMapping.resize(numberOfDevicesPerHost);
}

bool ServerConnectivityUserConfig::parseConfig(const std::string path)
{
    json config;

    // if provided embedded JSON configuration string, use it
    if (path.empty())
    {
        LOG_HCL_DEBUG(HCL, "JSON Config File path is empty. Using default settings");
        return false;
    }
    else
    {
        std::ifstream file(path);
        // input config file not found
        if (!file.good())
        {
            LOG_HCL_CRITICAL(HCL, "JSON Config File ({}) not found", path.c_str());
            return false;
        }

        try
        {
            file >> config;

            m_hasValidMapping = parseNics(path, config);
            if (m_hasValidMapping)
            {
                m_filePathLoaded = path;
            }
            return m_hasValidMapping;
        }
        catch (std::exception& e)
        {
            LOG_HCL_ERR(HCL, "Invalid json file {}, error {}", path.c_str(), e.what());
            return false;
        }
    }
}

bool ServerConnectivityUserConfig::parseNics(const std::string& path, const json& config)
{
    const std::vector<json> cards = config["HCL_NICS"].get<std::vector<json>>();
    if (!(cards.size() == m_numberOfDevicesPerHost))
    {
        LOG_HCL_CRITICAL(HCL, "JSON Config File ({}) number of cards not correct", path.c_str());
        return false;
    }
    std::unordered_set<unsigned> deviceIdsFound;
    for (auto card : cards)
    {
        const unsigned deviceId = card["CARD_LOCATION"].get<unsigned>();
        LOG_HCL_DEBUG(HCL, "deviceId={}", deviceId);
        if (!(deviceId >= 0 && deviceId < m_numberOfDevicesPerHost))
        {
            LOG_HCL_CRITICAL(HCL, "JSON Config File ({}) invalid CARD_LOCATION id {}", path.c_str(), deviceId);
            return false;
        }
        if (deviceIdsFound.count(deviceId))
        {
            LOG_HCL_CRITICAL(HCL, "JSON Config File ({}) device {} has multiple definitions", path.c_str(), deviceId);
            return false;
        }
        deviceIdsFound.insert(deviceId);
        const std::vector<json> nics = card["NICS"].get<std::vector<json>>();
        if (!(nics.size() == std::tuple_size<Gen2ArchNicsDeviceSingleConfig>::value))
        {
            LOG_HCL_CRITICAL(HCL,
                             "JSON Config File ({}) number of nics for device {} not correct",
                             path.c_str(),
                             deviceId);
            return false;
        }
        unsigned currNic = 0;
        for (const json& myNic : nics)
        {
            const int     remoteCard   = myNic["REMOTE_CARD"];
            const uint8_t remoteNic    = myNic["REMOTE_NIC"];
            const uint8_t remoteSubNic = myNic["REMOTE_SUB_NIC"];
            LOG_HCL_DEBUG(HCL,
                          "deviceId={}, currNic={}, remoteCard={}, remoteNic={}, remoteSubNic={}",
                          deviceId,
                          currNic,
                          remoteCard,
                          remoteNic,
                          remoteSubNic);

            if (!(((unsigned)remoteCard == SCALEOUT_DEVICE_ID) || ((unsigned)remoteCard == NOT_CONNECTED_DEVICE_ID) ||
                  ((unsigned)remoteCard >= 0 && (unsigned)remoteCard < m_numberOfDevicesPerHost)))
            {
                LOG_HCL_CRITICAL(HCL,
                                 "JSON Config File ({})  device {} NIC {} invalid REMOTE_CARD id {}",
                                 path.c_str(),
                                 deviceId,
                                 currNic,
                                 remoteCard);
                return false;
            }
            if ((unsigned)remoteCard == deviceId)
            {
                LOG_HCL_CRITICAL(HCL,
                                 "JSON Config File ({}) device {} NIC {} cannot have nics connected to self",
                                 path.c_str(),
                                 deviceId,
                                 currNic);
                return false;
            }

            if (!((unsigned)remoteNic >= 0 && (unsigned)remoteNic < MAX_NICS_GEN2ARCH))
            {
                LOG_HCL_CRITICAL(HCL,
                                 "JSON Config File ({}) device {} NIC {} has invalid REMOTE_NIC id {}",
                                 path.c_str(),
                                 deviceId,
                                 currNic,
                                 remoteNic);
                return false;
            }

            if (!(remoteSubNic >= 0 && remoteSubNic < MAX_SUB_NICS))
            {
                LOG_HCL_CRITICAL(HCL,
                                 "JSON Config File ({}) device {} NIC {} has invalid REMOTE_SUB_NIC id {}",
                                 path.c_str(),
                                 deviceId,
                                 currNic,
                                 remoteSubNic);
                return false;
            }

            m_customMapping[deviceId][currNic] = std::make_tuple(remoteCard, remoteNic, remoteSubNic);
            currNic++;
        }
    }

    return true;
}
