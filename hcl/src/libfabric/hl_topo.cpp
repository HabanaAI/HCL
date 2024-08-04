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
#include <lemon/lp.h>   // for Mip
#include "hcl_utils.h"  // for VERIFY, LOG_HCL_DEBUG, LOG_H...
#include "hcl_topology.h"

namespace hl_topo
{
using namespace lemon;

static std::string getPCIAddress(const hwloc_obj_t device)
{
    return fmt::format("{:02x}:{:02x}.{:01x}",
                       device->attr->pcidev.bus, device->attr->pcidev.dev, device->attr->pcidev.func);
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
    static std::map<hwloc_obj_t, uint32_t> device_module_id;
    const auto it = device_module_id.find(device);
    if (device_module_id.end()!= it)
    {
        return it->second;
    }

    const auto name = getOpenfabricName(device);
    const auto module_id_path = fmt::format("/sys/class/accel/accel{}/device//module_id", name.back());
    std::ifstream file(module_id_path);
    VERIFY(file.is_open(), "Failed to open accel module_id file");
    std::string line;
    std::getline(file, line);
    file.close();
    device_module_id[device] = std::stoul(line);
    return device_module_id[device];
}

struct HwlocOAMCompare {
    bool operator()(const hwloc_obj_t obj1, const hwloc_obj_t obj2) const
    {
        return getModuleId(obj1) < getModuleId(obj2);
    }
};

struct HwlocHNICCompare {
    bool operator()(const hwloc_obj_t obj1, const hwloc_obj_t obj2) const
    {
        const auto obj1_address = getPCIAddress(obj1);
        const auto obj2_address = getPCIAddress(obj2);
        return std::lexicographical_compare(obj1_address.cbegin(), obj1_address.cend(),
                                            obj2_address.cbegin(), obj2_address.cend());
    }
};

using HwObjMatrix  = std::map<hwloc_obj_t, std::map<hwloc_obj_t, hwloc_obj_t, HwlocHNICCompare>, HwlocOAMCompare>;
using WeightMatrix = std::map<hwloc_obj_t, std::map<hwloc_obj_t, uint32_t, HwlocHNICCompare>, HwlocOAMCompare>;

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

static uint32_t getTypeWeight(const hwloc_obj_type_t type)
{
    // The hwloc_obj_type_t enum uses high values for better connections, we want to use high values for worse
    // connections.
    return ((type * -1) + HWLOC_OBJ_TYPE_MAX) * 10;
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
static void printConnectionsMatrix(const HwObjMatrix& matrix)
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
static hwloc_obj_t getCommonAncestorObj(const hwloc_obj_t obj1, const hwloc_obj_t obj2){
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

static HwObjMatrix createConnectionMatrix(const hwloc_topology_t          topology,
                                          const std::vector<hwloc_obj_t>& oams,
                                          const std::vector<hwloc_obj_t>& hnics)
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

WeightMatrix createWeights(const HwObjMatrix& matrix)
{
    WeightMatrix weights;
    for (const auto& [oam, hnics] : matrix)
    {
        for (const auto& [hnic, parent] : hnics)
        {
            const auto weight  = getDistance(parent, oam, hnic) + getTypeWeight(parent->type);
            weights[oam][hnic] = weight;
        }
    }
    return weights;
}

static hwloc_obj_t findBestConnections(const WeightMatrix& weights, const hwloc_obj_t targetOam)
{
    Mip mip;  // Mixed-Integer Programming solver

    std::map<hwloc_obj_t, std::vector<Mip::Col>, HwlocOAMCompare>           oamVariables;
    std::map<hwloc_obj_t, std::vector<Mip::Col>, HwlocHNICCompare>          hnicVariables;
    std::map<Mip::Col, std::tuple<hwloc_obj_t, hwloc_obj_t>> variablesEdges;
    Mip::Expr                                                objective;
    for (const auto& [oam, hnics] : weights)
    {
        for (const auto& [hnic, weight] : hnics)
        {
            const auto variable = mip.addCol();
            mip.colType(variable, Mip::INTEGER);
            mip.colLowerBound(variable, 0);
            mip.colUpperBound(variable, 1);
            oamVariables[oam].push_back(variable);
            hnicVariables[hnic].push_back(variable);
            variablesEdges[variable] = std::make_tuple(oam, hnic);
            objective += (weight * variable);
        }
    }
    mip.min();
    mip.obj(objective);

    // Set constraint that each OAM must have exactly one connection
    for (const auto& [oam, variables] : oamVariables)
    {
        UNUSED(oam);
        Mip::Expr oamConnectionCount;
        for (const auto& variable : variables)
        {
            oamConnectionCount += variable;
        }
        mip.addRow(oamConnectionCount == 1);
    }

    const auto oamCount                 = weights.size();
    const auto hnicCount                = (*weights.cbegin()).second.size();
    const auto minConnectionCount       = oamCount / hnicCount;
    const auto remainderConnectionCount = (oamCount % hnicCount) ? 1 : 0;
    // Set constraint that each host NIC must have between minConnectionCount and minConnectionCount +
    // remainderConnectionCount
    for (const auto& [hnic, variables] : hnicVariables)
    {
        UNUSED(hnic);
        Mip::Expr oamConnectionCount;
        for (const auto& variable : variables)
        {
            oamConnectionCount += variable;
        }
        mip.addRow(minConnectionCount <= oamConnectionCount <= (minConnectionCount + remainderConnectionCount));
    }

    mip.solve();
    VERIFY((Mip::OPTIMAL == mip.type()), "Failed to find optimal OAM to HNIC pairing");
    const auto& v = oamVariables[targetOam];
    const auto  variable =
        std::find_if(v.cbegin(), v.cend(), [&mip](const auto& variable) { return mip.sol(variable) == 1; });
    VERIFY((variable != v.cend()));
    return std::get<1>(variablesEdges[*variable]);
}

static hwloc_obj_t getOam(const std::vector<hwloc_obj_t>& oams, const std::string& accel)
{
    static constexpr std::string_view ACCEL {"accel"};
    static constexpr std::string_view OAM {"hbl_"};

    VERIFY(accel.compare(0, ACCEL.length(), ACCEL) == 0,
           std::string("Invalid accel does not start with \"") + ACCEL.data() + "\" - " + accel);

    auto oam = accel;
    // Change accel4 to hbl_4
    oam.replace(accel.find(ACCEL), ACCEL.size(), OAM);

    const auto accelIt = std::find_if(oams.cbegin(), oams.cend(), [&oam](const hwloc_obj_t& obj) {
        return getOpenfabricName(obj) == oam;
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
    printConnectionsMatrix(connectionMatrix);

    const auto weights = createWeights(connectionMatrix);

    const auto oam  = getOam(oams, accel);
    const auto hnic = findBestConnections(weights, oam);

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
