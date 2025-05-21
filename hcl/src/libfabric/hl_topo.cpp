#include "hl_topo.h"
#include <vector>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string_view>
#include <map>
#include <optional>
#include <hwloc.h>
#include "hcl_utils.h"  // for VERIFY, LOG_HCL_DEBUG, LOG_H...
#include "hcl_topology.h"
#include <regex>
#if !defined __GNUC__ || __GNUC__ >= 8
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

namespace hl_topo
{

static std::map<std::string, uint32_t>    pci_addr_to_module_id;
static std::map<std::string, std::string> pci_addr_to_accel;

static std::string getPCIAddress(const hwloc_obj_t device)
{
    return fmt::format("{:04x}:{:02x}:{:02x}.{:01x}",
                       device->attr->pcidev.domain,
                       device->attr->pcidev.bus,
                       device->attr->pcidev.dev,
                       device->attr->pcidev.func);
}

static void mapPCIAddressToModuleIdAndAccel()
{
    const std::string path = "/sys/class/accel/";
    std::regex        accelRegex(R"(accel\d+)");
    for (const auto& entry : fs::directory_iterator(path))
    {
        if (fs::is_directory(entry.path()))
        {
            const auto  dirName = entry.path().filename().string();
            std::smatch match;
            if (std::regex_search(dirName, match, accelRegex))
            {
                const auto accel = match.str();

                const auto    pci_addr_path = entry.path().string() + "/device/pci_addr";
                std::ifstream file_pci_addr(pci_addr_path);
                VERIFY(file_pci_addr.is_open(), "Failed to open {} pci_addr file", accel);
                std::string pci_addr;
                std::getline(file_pci_addr, pci_addr);
                file_pci_addr.close();

                const auto    module_id_path = entry.path().string() + "/device/module_id";
                std::ifstream file_module_id(module_id_path);
                VERIFY(file_module_id.is_open(), "Failed to open {} module_id file", accel);
                std::string module_id;
                std::getline(file_module_id, module_id);
                file_module_id.close();

                pci_addr_to_module_id[pci_addr] = std::stoul(module_id);
                pci_addr_to_accel[pci_addr]     = accel;
            }
        }
    }
}

static hwloc_obj_t getOSDevice(const hwloc_obj_t device, const hwloc_obj_osdev_type_t type)
{
    hwloc_obj_t osDevice = device->io_first_child;
    while (osDevice)
    {
        if (type == osDevice->attr->osdev.type)
        {
            return osDevice;
        }
        osDevice = osDevice->next_sibling;
    }
    throw hcl::VerifyException("Failed to find OS device for PCI device");
}

static hwloc_obj_t getOpenfabricOSDevice(const hwloc_obj_t device)
{
    return getOSDevice(device, HWLOC_OBJ_OSDEV_OPENFABRICS);
}

static hwloc_obj_t getNetworkOSDevice(const hwloc_obj_t device)
{
    return getOSDevice(device, HWLOC_OBJ_OSDEV_NETWORK);
}

static std::string getOpenfabricName(const hwloc_obj_t device)
{
    return getOpenfabricOSDevice(device)->name;
}

static uint32_t getModuleId(const hwloc_obj_t device)
{
    if (pci_addr_to_module_id.empty())
    {
        mapPCIAddressToModuleIdAndAccel();
    }
    return pci_addr_to_module_id[getPCIAddress(device)];
}

struct HwlocOAMCompare
{
    bool operator()(const hwloc_obj_t obj1, const hwloc_obj_t obj2) const
    {
        return getModuleId(obj1) < getModuleId(obj2);
    }
};

struct HwlocHNICCompare
{
    bool operator()(const hwloc_obj_t obj1, const hwloc_obj_t obj2) const
    {
        const auto obj1_address = getPCIAddress(obj1);
        const auto obj2_address = getPCIAddress(obj2);
        return std::lexicographical_compare(obj1_address.cbegin(),
                                            obj1_address.cend(),
                                            obj2_address.cbegin(),
                                            obj2_address.cend());
    }
};

using HwObjMatrix = std::map<hwloc_obj_t, std::map<hwloc_obj_t, hwloc_obj_t, HwlocHNICCompare>, HwlocOAMCompare>;

static constexpr uint16_t HABANA_LABS_VENDOR_ID {0x1da3};
static constexpr uint16_t MELLANOX_VENDOR_ID {0x15b3};
static constexpr uint16_t BROADCOM_VENDOR_ID {0x14e4};
static constexpr uint8_t  CELL_WIDTH {14};

static bool isHNICActive(const hwloc_obj_t device)
{
    static constexpr std::string_view STATE_NAME   = "Port1State";
    static constexpr std::string_view STATE_ACTIVE = "4";

    try
    {
        const hwloc_obj_t osDevice = getOpenfabricOSDevice(device);
        return STATE_ACTIVE == hwloc_obj_get_info_by_name(osDevice, STATE_NAME.data());
    }
    catch (const hcl::VerifyException& e)
    {
        LOG_DEBUG(HCL_OFI, "HNIC ({}) missing Openfabric OS device", getPCIAddress(device));
    }

    return false;
}

static uint32_t getDistance(const hwloc_obj_t parent, const hwloc_obj_t obj1, const hwloc_obj_t obj2)
{
    return (parent->depth - obj1->depth) + (parent->depth - obj2->depth) - 1;
}

/**
 * @brief Query the topology for PCI by Habana Labs, Mellanox and Broadcom PCI devices.
 *
 * @param topology hwloc topology object
 * @return 2D Map with OAMs as row and HNICs as columns where the cell contains the connection object.
 */
static std::tuple<std::vector<hwloc_obj_t>, std::vector<hwloc_obj_t>> findPciDevices(const hwloc_topology_t topology)
{
    std::vector<hwloc_obj_t> oams {};
    std::vector<hwloc_obj_t> hnics {};
    hwloc_obj_t              pciDevice = nullptr;
    while ((pciDevice = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_PCI_DEVICE, pciDevice)))
    {
        switch (pciDevice->attr->pcidev.vendor_id)
        {
            case HABANA_LABS_VENDOR_ID:
                oams.push_back(pciDevice);
                break;

            case BROADCOM_VENDOR_ID:
            case MELLANOX_VENDOR_ID:
                if (!isHNICActive(pciDevice))
                {
                    break;
                }
                hnics.push_back(pciDevice);
                break;

            default:
                break;
        }
    }

    return {oams, hnics};
}

/** For example:
ModuleID        mlx5_0        mlx5_2
       0       Package       Machine
       1       Package       Machine
       2       Package       Machine
       3       Package       Machine
       4       Machine       Package
       5       Machine       Package
       6       Machine       Package
       7       Machine       Package
*/
static void printConnectionsMatrix(const HwObjMatrix&                                   matrix,
                                   std::map<hwloc_obj_t, hwloc_obj_t, HwlocOAMCompare>& oam2hnic)
{
    std::stringstream matrixSS;
    matrixSS << std::setw(CELL_WIDTH) << "ModuleID";
    // Print matrix column headers
    for (const auto& [hnic, _] : (*matrix.cbegin()).second)
    {
        UNUSED(_);
        matrixSS << std::setw(CELL_WIDTH) << getOpenfabricName(hnic);
    }

    matrixSS << std::endl;
    for (const auto& [oam, hnics] : matrix)
    {
        matrixSS << std::setw(CELL_WIDTH) << getModuleId(oam);  // Print OAM column
        for (const auto& [hnic, parent] : hnics)
        {
            std::stringstream cell_ss;
            if (oam2hnic[oam] == hnic)
            {
                cell_ss << "* ";
            }
            cell_ss << hwloc_obj_type_string(parent->type);
            if (parent->type == HWLOC_OBJ_BRIDGE)
            {
                const auto distance = getDistance(parent, oam, hnic);
                cell_ss << "(" << distance << ")";
            }
            matrixSS << std::setw(CELL_WIDTH) << cell_ss.str();
        }
        matrixSS << std::endl;
    }

    LOG_INFO(HCL_OFI, "Topology matrix:\n{}", matrixSS.str());
}

std::vector<hwloc_obj_t> getParentsList(hwloc_obj_t obj)
{
    std::vector<hwloc_obj_t> parents;

    while (obj)
    {
        parents.push_back(obj);
        obj = obj->parent;
    }
    return parents;
}

/**
 * @brief Replaces hwloc's implementation of hwloc_get_common_ancestor_obj to ignore depth parameter.
 *
 * @param obj1 hwloc object
 * @param obj2 hwloc object
 * @return First common ancestor in the parent linked list.
 */
static hwloc_obj_t getCommonAncestorObj(const hwloc_obj_t obj1, const hwloc_obj_t obj2)
{
    const std::vector<hwloc_obj_t> parents1 = getParentsList(obj1);
    const std::vector<hwloc_obj_t> parents2 = getParentsList(obj2);

    const auto common_ancestor_length = std::min(parents1.size(), parents2.size());
    for (size_t index = 0; index < common_ancestor_length; ++index)
    {
        if (parents1[parents1.size() - 1 - index] != parents2[parents2.size() - 1 - index])
        {
            // Once we encounter an ancestor
            return parents1[parents1.size() - index];
        }
    }
    VERIFY(false, "Failed to find common ancestor");
}

static HwObjMatrix createConnectionMatrix([[maybe_unused]] const hwloc_topology_t topology,
                                          const std::vector<hwloc_obj_t>&         oams,
                                          const std::vector<hwloc_obj_t>&         hnics)
{
    HwObjMatrix connectionMatrix;
    for (const auto& oam : oams)
    {
        for (const auto& hnic : hnics)
        {
            const auto parent           = getCommonAncestorObj(oam, hnic);
            connectionMatrix[oam][hnic] = parent;
        }
    }
    return connectionMatrix;
}

static hwloc_obj_t findBestConnections(const HwObjMatrix& connectionMatrix, const hwloc_obj_t targetOam)
{
    std::map<hwloc_obj_t, int, HwlocHNICCompare>        hnicConnectionsCntr;
    std::map<hwloc_obj_t, hwloc_obj_t, HwlocOAMCompare> oam2hnic;

    for (const auto& [hnic, parent] : connectionMatrix.at(targetOam))
    {
        UNUSED(parent);
        hnicConnectionsCntr[hnic] = 0;
    }

    uint32_t maxBridgeDistance = 0;
    for (const auto& [oam, hnics] : connectionMatrix)
    {
        for (const auto& [hnic, parent] : hnics)
        {
            if (parent->type == HWLOC_OBJ_BRIDGE)
            {
                maxBridgeDistance = std::max(maxBridgeDistance, getDistance(parent, oam, hnic));
            }
        }
    }

    for (const auto& connectionType : {HWLOC_OBJ_BRIDGE, HWLOC_OBJ_PACKAGE, HWLOC_OBJ_MACHINE})
    {
        uint32_t distances = connectionType == HWLOC_OBJ_BRIDGE ? maxBridgeDistance : 1;
        for (uint32_t i = 1; i <= distances; i++)
        {
            for (const auto& [oam, hnics] : connectionMatrix)
            {
                hwloc_obj_t bestHnic = nullptr;
                if (oam2hnic.find(oam) != oam2hnic.end()) continue;

                for (const auto& [hnic, parent] : hnics)
                {
                    if (parent->type == connectionType &&
                        (connectionType != HWLOC_OBJ_BRIDGE || i == getDistance(parent, oam, hnic)))
                    {
                        if (!bestHnic || hnicConnectionsCntr[hnic] < hnicConnectionsCntr[bestHnic])
                        {
                            bestHnic = hnic;
                        }
                    }
                }
                if (bestHnic)
                {
                    hnicConnectionsCntr[bestHnic] += 1;
                    oam2hnic[oam] = bestHnic;
                }
            }
        }
    }

    printConnectionsMatrix(connectionMatrix, oam2hnic);

    return oam2hnic[targetOam];
}

static hwloc_obj_t getOam(const std::vector<hwloc_obj_t>& oams, const std::string& accel)
{
    static constexpr std::string_view ACCEL {"accel"};
    VERIFY(accel.compare(0, ACCEL.length(), ACCEL) == 0,
           std::string("Invalid accel does not start with \"") + ACCEL.data() + "\" - " + accel);
    if (pci_addr_to_accel.empty())
    {
        mapPCIAddressToModuleIdAndAccel();
    }
    const auto accelIt = std::find_if(oams.cbegin(), oams.cend(), [&accel](const hwloc_obj_t& obj) {
        return pci_addr_to_accel[getPCIAddress(obj)] == accel;
    });
    VERIFY((accelIt != oams.cend()), "Failed to find given accel");

    return *accelIt;
}

std::vector<hwloc_obj_t> filterSuitable(const std::vector<hwloc_obj_t>&     hnics,
                                        const std::vector<struct fi_info*>& providers)
{
    std::vector<hwloc_obj_t> suitableHnics;
    // Copy hnic from hnics to suitableHnics if hnic in providers
    std::copy_if(
        hnics.cbegin(),
        hnics.cend(),
        std::back_inserter(suitableHnics),
        [&providers](const hwloc_obj_t& hnic) {
            return std::any_of(providers.cbegin(), providers.cend(), [&hnic](const struct fi_info* const provider) {
                return getOpenfabricName(hnic) == provider->nic->device_attr->name;
            });
        });
    return suitableHnics;
}

std::string translateHwlocType(const hwloc_obj_type_t hwlocType)
{
    static const std::unordered_map<hwloc_obj_type_t, std::string_view> TRANSLATIONS {
        {HWLOC_OBJ_BRIDGE, "PCI switch"},
        {HWLOC_OBJ_PACKAGE, "NUMA node"},
        {HWLOC_OBJ_MACHINE, "different NUMA nodes"}};
    const auto translation = TRANSLATIONS.find(hwlocType);
    if (translation != TRANSLATIONS.cend())
    {
        return translation->second.data();
    }

    return hwloc_obj_type_string(hwlocType);
}

std::tuple<size_t, std::string> getBestProvider(const std::vector<struct fi_info*>& providers, const std::string& accel)
{
    VERIFY(!providers.empty(), "Providers list is empty");

    HwlocTopology topology;
    const auto [oams, hnics] = findPciDevices(*topology);

    if (oams.empty())
    {
        // In simulator there are no OAMs
        return {0, ""};
    }

    const auto suitableHnics = filterSuitable(hnics, providers);

    VERIFY(!suitableHnics.empty(), "The are no suitable providers");

    const auto connectionMatrix = createConnectionMatrix(*topology, oams, suitableHnics);

    const auto oam  = getOam(oams, accel);
    const auto hnic = findBestConnections(connectionMatrix, oam);

    const auto        connection = connectionMatrix.at(oam).at(hnic);
    const std::string matchType  = translateHwlocType(connection->type);
    // Return the index of the correct fi_info*
    const auto index =
        std::distance(providers.cbegin(),
                      std::find_if(providers.cbegin(), providers.cend(), [&hnic](const fi_info* const provider) {
                          return getOpenfabricName(hnic) == provider->nic->device_attr->name;
                      }));
    return {index, matchType};
}

std::unordered_map<const struct fi_info*, std::string>
getProviderInterface(const std::vector<struct fi_info*>& providers)
{
    VERIFY(!providers.empty(), "Providers list is empty");

    HwlocTopology topology;
    const auto [oams, hnics] = findPciDevices(*topology);
    UNUSED(oams);

    std::unordered_map<const struct fi_info*, std::string> provider_interfaces;
    for (const struct fi_info* const provider : providers)
    {
        for (const hwloc_obj_t& hnic : hnics)
        {
            if (getOpenfabricName(hnic) == provider->nic->device_attr->name)
            {
                provider_interfaces[provider] = getNetworkOSDevice(hnic)->name;
                break;
            }
        }
    }

    return provider_interfaces;
}

}  // namespace hl_topo
