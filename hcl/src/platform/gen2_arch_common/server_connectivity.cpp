#include "platform/gen2_arch_common/server_connectivity.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t
#include <memory>   // for allocator_traits<>::value_type

#include "platform/gen2_arch_common/types.h"                      // for GEN2ARCH_HLS_BOX_SIZE
#include "platform/gen2_arch_common/server_connectivity_types.h"  // for Gen2ArchNicsDeviceSingleConfig, ServerNicsConnectivityArray
#include "platform/gen2_arch_common/server_connectivity_user_config.h"  // for ServerConnectivityUserConfig
#include "platform/gen2_arch_common/runtime_connectivity.h"             // for Gen2ArchRuntimeConnectivity
#include "platform/gen2_arch_common/hcl_device_config.h"                // for HclDeviceConfig

#include "hcl_utils.h"            // for VERIFY
#include "ibverbs/hcl_ibverbs.h"  // for g_ibv
#include "hcl_log_manager.h"      // for LOG_*

// Some default values for uint tests ctor

static const Gen2ArchNicsDeviceSingleConfig s_dummyTestDeviceSingleConfig = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),   // NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),   // NIC=1
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 2, 2),   // NIC=2
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 3, 0),   // NIC=3
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 4, 1),   // NIC=4
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 5, 2),   // NIC=5
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 6, 0),   // NIC=6
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 7, 1),   // NIC=7
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 8, 2),   // NIC=8
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 9, 0),   // NIC=9
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 10, 1),  // NIC=10
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 11, 2),  // NIC=11
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 12, 0),  // NIC=12
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 13, 1),  // NIC=13
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 14, 2),  // NIC=14
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 15, 0),  // NIC=15
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 16, 1),  // NIC=16
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 17, 2),  // NIC=17
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 18, 0),  // NIC=18
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 19, 1),  // NIC=19
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 20, 2),  // NIC=20
    std::make_tuple(SCALEOUT_DEVICE_ID, 21, 0),       // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),       // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),       // NIC=23
};

const ServerNicsConnectivityArray g_dummyTestDeviceServerNicsConnectivity = {s_dummyTestDeviceSingleConfig,
                                                                             s_dummyTestDeviceSingleConfig,
                                                                             s_dummyTestDeviceSingleConfig,
                                                                             s_dummyTestDeviceSingleConfig,
                                                                             s_dummyTestDeviceSingleConfig,
                                                                             s_dummyTestDeviceSingleConfig,
                                                                             s_dummyTestDeviceSingleConfig,
                                                                             s_dummyTestDeviceSingleConfig};

Gen2ArchServerConnectivity::Gen2ArchServerConnectivity(const int                          fd,
                                                       const int                          moduleId,
                                                       const bool                         useDummyConnectivity,
                                                       const ServerNicsConnectivityArray& serverNicsConnectivityArray,
                                                       HclDeviceConfig&                   deviceConfig)
: m_fd(fd),
  m_moduleId(moduleId),
  m_useDummyConnectivity(useDummyConnectivity),
  m_serverNicsConnectivityArray(serverNicsConnectivityArray),
  m_deviceConfig(deviceConfig)
{
    LOG_HCL_DEBUG(HCL, "ctor, fd={}, moduleId={}, useDummyConnectivity={}", fd, moduleId, useDummyConnectivity);
    if (fd >= 0)
    {
        VERIFY(moduleId < GEN2ARCH_HLS_BOX_SIZE, "Unexpected module id {}", moduleId);
    }
}

void Gen2ArchServerConnectivity::init(const bool readLkdPortsMask)
{
    LOG_HCL_DEBUG(HCL, "Started, m_moduleId={}, m_fd={}, readLkdPortsMask={}", m_moduleId, m_fd, readLkdPortsMask);
    if (readLkdPortsMask)
    {
        readDeviceLkdPortsMask();
    }

    m_userScaleOutPortsMask = GCFG_SCALE_OUT_PORTS_MASK.value();
    LOG_HCL_DEBUG(HCL, "m_userScaleOutPortsMask={:024b}", m_userScaleOutPortsMask);

    if (m_lkdPortsMaskValid)
    {
        VERIFY(m_lkdPortsMasks.hwPortsMask != INVALID_PORTS_MASK, "Internal ports mask was not defined.");
        m_enabledPortsMask        = m_lkdPortsMasks.hwPortsMask;
        m_lkdEnabledScaleoutPorts = m_lkdPortsMasks.hwExtPortsMask;
        m_maxScaleoutPorts        = m_lkdEnabledScaleoutPorts.count();
        LOG_HCL_DEBUG(HCL,
                      "m_enabled_ports_mask={:024b}, m_lkd_enabled_scaleout_ports={:024b}, m_max_scaleout_ports={}",
                      (uint64_t)m_enabledPortsMask,
                      (uint64_t)m_lkdEnabledScaleoutPorts,
                      m_maxScaleoutPorts);
    }

    m_usersConnectivityConfig.parseConfig(
        GCFG_HCL_PORT_MAPPING_CONFIG
            .value());  // parse json port mapping file if exists. It will replace default comm configuration

    m_commsRuntimeConnectivity.push_back(nullptr);
    m_commsRuntimeConnectivity[DEFAULT_COMM_ID].reset(createRuntimeConnectivityFactory(m_moduleId,
                                                                                       DEFAULT_COMM_ID,  // hclCommId,
                                                                                       *this));
    m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->init(m_serverNicsConnectivityArray,
                                                      m_usersConnectivityConfig,
                                                      readLkdPortsMask);
}

void Gen2ArchServerConnectivity::readDeviceLkdPortsMask()
{
    // Get port mask from LKD if we can
    g_ibv.get_port_mask(m_lkdPortsMasks);
    m_lkdPortsMaskValid = true;
}

int Gen2ArchServerConnectivity::getRemoteDevice(const uint16_t port, [[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getRemoteDevice(port);
}

uint16_t Gen2ArchServerConnectivity::getPeerPort(const uint16_t port, [[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getPeerPort(port);
}

uint16_t Gen2ArchServerConnectivity::getSubPortIndex(const uint16_t                  port,
                                                     [[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getSubPortIndex(port);
}

uint16_t Gen2ArchServerConnectivity::getScaleoutNicFromSubPort(const uint16_t                  subPort,
                                                               [[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getScaleoutNicFromSubPort(subPort);
}

bool Gen2ArchServerConnectivity::isScaleoutPort(const uint16_t port, [[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->isScaleoutPort(port);
}

uint16_t Gen2ArchServerConnectivity::getMaxSubPort(const bool isScaleoutPort) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getMaxSubPort(isScaleoutPort);
}

nics_mask_t Gen2ArchServerConnectivity::getAllPortsGlbl(const int                       deviceId,
                                                        [[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getAllPortsGlbl(deviceId);
}

nics_mask_t Gen2ArchServerConnectivity::getAllPorts(const int         deviceId,
                                                    const nics_mask_t enabledExternalPortsMask) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getAllPorts(deviceId, enabledExternalPortsMask);
}

nics_mask_t Gen2ArchServerConnectivity::getScaleOutPortsGlbl([[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getScaleOutPortsGlbl();
}

nics_mask_t Gen2ArchServerConnectivity::getScaleUpPorts([[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getScaleUpPorts();
}

uint16_t Gen2ArchServerConnectivity::getDefaultScaleUpPort([[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getDefaultScaleUpPort();
}

uint64_t Gen2ArchServerConnectivity::getExternalPortsMaskGlbl([[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getExternalPortsMaskGlbl();
}

uint16_t Gen2ArchServerConnectivity::getNumScaleUpPorts([[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getNumScaleUpPorts();
}

uint16_t Gen2ArchServerConnectivity::getNumScaleOutPortsGlbl([[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getNumScaleOutPortsGlbl();
}

uint16_t Gen2ArchServerConnectivity::getScaleoutSubPortIndexGlbl(const uint16_t                  port,
                                                                 [[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getScaleoutSubPortIndexGlbl(port);
}

bool Gen2ArchServerConnectivity::isUpdateScaleOutGlobalContextRequired([[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->isUpdateScaleOutGlobalContextRequired();
}

uint16_t Gen2ArchServerConnectivity::getDefaultScaleOutPortByIndex(const uint16_t nicIdx) const
{
    return m_lkdEnabledScaleoutPorts(nicIdx);
}

const nics_mask_t Gen2ArchServerConnectivity::getAllScaleoutPorts([[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getAllScaleoutPorts();
}

uint32_t Gen2ArchServerConnectivity::getBackpressureOffset(const uint16_t                  nic,
                                                           [[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getBackpressureOffset(nic);
}

uint16_t Gen2ArchServerConnectivity::getMaxNumScaleUpPortsPerConnection([[maybe_unused]] const HCL_Comm hclCommId) const
{
    return m_commsRuntimeConnectivity[DEFAULT_COMM_ID]->getMaxNumScaleUpPortsPerConnection();
}

void Gen2ArchServerConnectivity::setUnitTestsPortsMasks(const nics_mask_t fullScaleoutPorts,
                                                        const nics_mask_t allPortsMask)
{
    m_enabledPortsMask        = allPortsMask;
    m_lkdEnabledScaleoutPorts = fullScaleoutPorts;
    m_maxScaleoutPorts        = fullScaleoutPorts.count();
    LOG_HCL_DEBUG(HCL,
                  "m_enabled_ports_mask={:024b}, m_lkd_enabled_scaleout_ports={:024b}, m_max_scaleout_ports={}",
                  (uint64_t)m_enabledPortsMask,
                  (uint64_t)m_lkdEnabledScaleoutPorts,
                  m_maxScaleoutPorts);
}
