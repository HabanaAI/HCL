#include "platform/gen2_arch_common/port_mapping_config.h"

#include <cstddef>        // for size_t
#include <cstdint>        // for uint8_t
#include <memory>         // for allocator_traits<>::value_type
#include <unordered_set>  // for unordered_set
#include <fstream>        // for ifstream

#include "hcl_utils.h"        // for LOG_HCL_*
#include "hcl_log_manager.h"  // for LOG_*
#include <nlohmann/json.hpp>  // for json
#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH, GEN2ARCH_HLS_BOX_SIZE

using json = nlohmannV340::json;

void logPortMappingConfig(const Gen2ArchNicsBoxConfig& mapping)
{
    unsigned deviceIndex = 0;
    for (auto& device : mapping)
    {
        unsigned nicIndex = 0;
        for (auto& tuple : device)
        {
            const int     remoteDeviceId = std::get<0>(tuple);
            const uint8_t remoteNicId    = std::get<1>(tuple);
            const uint8_t remoteSubNicId = std::get<2>(tuple);
            LOG_TRACE(HCL,
                      "Gen2ArchNicsBoxConfig Mapping: [{}][{}] = [[ {}, {}, {} ]]",
                      deviceIndex,
                      nicIndex,
                      remoteDeviceId,
                      remoteNicId,
                      remoteSubNicId);
            nicIndex++;
        }
        deviceIndex++;
    }
}

bool Gen2ArchPortMappingConfig::parseConfig(const std::string path)
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

bool Gen2ArchPortMappingConfig::parseNics(const std::string& path, const json& config)
{
    m_spotlightType = config["SPOTLIGHT_TYPE"].get<int>();
    if (m_spotlightType > MAX_SPOTLIGHT)
    {
        LOG_HCL_CRITICAL(HCL, "JSON Config File ({}) spotlight type is not correct", m_spotlightType);
        return false;
    }
    const std::vector<json> cards = config["HCL_NICS"].get<std::vector<json>>();
    if (!(cards.size() == std::tuple_size<Gen2ArchNicsBoxConfig>::value))
    {
        LOG_HCL_CRITICAL(HCL, "JSON Config File ({}) number of cards not correct", path.c_str());
        return false;
    }
    std::unordered_set<unsigned> deviceIdsFound;
    for (auto card : cards)
    {
        const unsigned deviceId = card["CARD_LOCATION"].get<unsigned>();
        LOG_HCL_DEBUG(HCL, "deviceId={}", deviceId);
        if (!(deviceId >= 0 && deviceId < GEN2ARCH_HLS_BOX_SIZE))
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
        if (!(nics.size() == std::tuple_size<Gen2ArchNicsDeviceConfig>::value))
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
                  ((unsigned)remoteCard >= 0 && (unsigned)remoteCard < GEN2ARCH_HLS_BOX_SIZE)))
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

const Gen2ArchNicsBoxConfig& Gen2ArchPortMappingConfig::getMapping() const
{
    return m_customMapping;
}

const unsigned Gen2ArchPortMappingConfig::getSpotlightType() const
{
    return m_spotlightType;
}
