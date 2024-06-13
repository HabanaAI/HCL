#include "hl_ofi.h"

#include <cassert>                       // for assert
#include <cstdint>                       // for uint64_t, uint32_t
#include <cstdio>                        // for fclose, fopen, fread, FILE
#include <cstdlib>                       // for NULL, free, getenv, setenv
#include <cstring>                       // for strtok, strdup, strlen, strncmp
#include <algorithm>                     // for copy, fill, max, find
#include <libgen.h>                      // for basename, dirname
#include <linux/limits.h>                // for PATH_MAX
#include <memory>                        // for unique_ptr
#include <regex>                         // for regex, cregex_iterator, cmatch
#include "hccl/network_utils.h"          // for get_desired_tcp_if_from_env_var
#include "hccl_ofi_wrapper_interface.h"  // for ofi_plugin_interface
#include "hccl_types.h"                  // for hcclLibfabricError, hcclSuccess
#include "hcl_utils.h"                   // for LOG_HCL_ERR, LOG_HCL_DEBUG
#include "hcl_log_manager.h"             // for LOG_*
#include "rdma/fabric.h"                 // for fi_info, fi_domain_attr, fi_...
#include "rdma/fi_errno.h"               // for FI_ENODATA
#include "hl_ofi_component.h"            // for ofi_component_t, ofiComm_t, list...
#include "hl_ofi_rdm_component.h"        // for ofi_rdm_component_t
#include "hl_ofi_msg_component.h"        // for ofi_msg_component_t
#include "hl_ofi_param.h"                // for hl_ofi_exclude_tcp_if
#include "hl_topo.h"
#include <sys/utsname.h>  // for getting kernel version

#define VERBS_PCI_PATH "/sys/class/infiniband/"
#define PCI_PATH       "/sys/bus/pci/devices/"
#define PCI_ADDR_LEN   12  // len(0000:00:00.0) == 12

bool ofi_t::s_mrLocal     = false;
bool ofi_t::s_hmemMR      = false;
bool ofi_t::s_gaudiDirect = false;
bool ofi_t::s_verbs       = false;

std::unique_ptr<ofi_plugin_interface> ofi_plugin;

/**
 * @brief The order prioritizes the providers
 *
 */
enum class provider_priority
{
    NONE  = 0,
    GAUDI = NONE,
    TCP,
    VERBS,
    EFA,
    BEST_PROV = EFA
};

/**
 * @brief Check if a given address is a valid BDF PCI address format.
 *
 * Example: 0000:08:00.0
 *
 * @param address
 * @return true if valid
 * @return false otherwise
 */
static bool isValidPCIAddress(const std::string& address)
{
    try
    {
        std::regex regex("^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\\.[0-9a-fA-F]$");
        return std::regex_match(address, regex);
    }
    catch (const std::regex_error& e)
    {
        LOG_ERR(HCL_OFI, "Regex error: {}", e.what());
        return false;
    }
}

/**
 * @brief Get the pci switch addr prefix
 *
 * @param endpoint_addr
 * @return PCI switch address
 */
static std::string get_pci_switch_addr_prefix(const std::string& endpoint_addr)
{
    // e.g. /sys/bus/pci/devices/0000:08:00.0
    std::string pci_addr_filename = fmt::format(PCI_PATH "{}", endpoint_addr);
    char        full_path[PATH_MAX];
    std::string switch_addr = {0};
    // e.g. /sys/devices/pci0000:05/0000:05:01.0/0000:06:00.0/0000:07:00.0/0000:08:00.0
    if (realpath(pci_addr_filename.c_str(), full_path) == nullptr)
    {
        LOG_WARN(HCL_OFI, "unable to read link of pci_addr file {}", pci_addr_filename);
        return "";
    }
    switch_addr = basename(dirname(full_path));
    if (isValidPCIAddress(switch_addr))
    {
        return switch_addr.substr(0, 7);
    }
    else
    {
        LOG_TRACE(HCL_OFI, "Found no PCI format matching, using endpoint_addr", endpoint_addr);
        return endpoint_addr.substr(0, 7);
    }
}

/**
 * @brief Get the full pci addr.
 *
 * @param endpoint_addr pci address of the ep
 * @return full pci path
 */
static std::string get_full_pci_addr(const std::string& endpoint_addr)
{
    // e.g. /sys/bus/pci/devices/0000:08:00.0
    std::string pci_addr_filename = fmt::format(PCI_PATH "{}", endpoint_addr);
    char        full_path[PATH_MAX];
    // e.g. /sys/devices/pci0000:05/0000:05:01.0/0000:06:00.0/0000:07:00.0/0000:08:00.0
    if (realpath(pci_addr_filename.c_str(), full_path) == nullptr)
    {
        LOG_WARN(HCL_OFI, "unable to read link of pci_addr file {}", pci_addr_filename);
        return "";
    }
    size_t             running_idx   = 0;
    size_t             token_idx     = 0;
    std::string        full_path_str = std::string(full_path);
    std::istringstream ss(full_path_str);

    std::string token;
    while (std::getline(ss, token, '/'))
    {
        token_idx = full_path_str.find(token, running_idx);
        if (isValidPCIAddress(token))
        {
            break;
        }
        running_idx = token_idx + token.length();
    }
    // start the full path from the first bdf, e.g.: 0000:05:01.0/0000:06:00.0/0000:07:00.0/0000:08:00.0
    return full_path_str.substr(token_idx);
}

/**
 * @brief Get the gaudi endpoint pci addr.
 *
 * @param device_fd
 * @return pci address (e.g. 0000:10:00.0)
 */
static std::string get_gaudi_pci_ep_addr(int device_fd)
{
    char pci_addr[PCI_ADDR_LEN + 1] = {0};
    int  rc                         = hlthunk_get_pci_bus_id_from_fd(device_fd, pci_addr, PCI_ADDR_LEN + 1);
    if (rc != 0)
    {
        LOG_ERR(HCL_OFI, "unable to read PCIe address of device {}. error: {}", device_fd, rc);
    }
    if (isValidPCIAddress(pci_addr))
    {
        return pci_addr;
    }
    return "";
}

/**
 * @brief Get the verbs endpoint pci addr.
 *
 * @param domain verbs provider domain name
 * @return endpoint pci address
 */
static std::string get_verbs_pci_ep_addr(const std::string& domain)
{
    // e.g. /sys/class/infiniband/mlx5_0/device
    std::string pci_addr_filename   = fmt::format(VERBS_PCI_PATH "{}/device", domain);
    char        full_path[PATH_MAX] = {0};
    std::string ep_addr             = {0};
    // e.g. /sys/devices/pci0000:05/0000:05:01.0/0000:06:00.0/0000:07:00.0/0000:08:00.0
    if (realpath(pci_addr_filename.c_str(), full_path) == nullptr)
    {
        LOG_WARN(HCL_OFI, "unable to read link of verbs pci_addr file {}", pci_addr_filename);
        return "";
    }
    // e.g. 0000:08:00.0
    ep_addr = basename(full_path);
    if (isValidPCIAddress(ep_addr))
    {
        return ep_addr;
    }
    else
    {
        LOG_WARN(HCL_OFI, "Invalid pci address: {}", ep_addr);
    }
    return "";
}

/**
 * @brief Get the efa endpoint pci addr
 *
 * @param bus_attr
 * @return pci address
 */
static std::string get_efa_pci_ep_addr(struct fi_bus_attr* bus_attr)
{
    if (bus_attr)
    {
        return fmt::format("{:04d}:{:2x}:{:2x}.{:x}",
                           bus_attr->attr.pci.domain_id,
                           bus_attr->attr.pci.bus_id,
                           bus_attr->attr.pci.device_id,
                           bus_attr->attr.pci.function_id);
    }
    return "";
}

static int get_numa_node(const std::string& pci_addr)
{
    int         numa_node          = -1;
    std::string numa_node_filename = fmt::format("/sys/bus/pci/devices/{}/numa_node", pci_addr);
    FILE*       numa_node_file     = fopen(numa_node_filename.c_str(), "rb");
    if (numa_node_file == nullptr)
    {
        LOG_WARN(HCL_OFI, "unable to open numa_node file {}", numa_node_filename);
        return numa_node;
    }
    char numa_id[2] = {0};
    int  result     = fread(numa_id, 1, 1, numa_node_file);
    fclose(numa_node_file);
    if (result != 1)
    {
        LOG_WARN(HCL_OFI, "unable to read numa_node file");
        return numa_node;
    }

    try
    {
        numa_node = std::stoi(numa_id);
    }
    catch (const std::exception& e)
    {
    }

    return numa_node;
}

static int get_cpuid_in_numa(int numa_node)
{
    int cpuid = -1;
    // File contains a range of CPUs, for example "0-39,80-119". We'll take the last CPU in the list (119).
    std::string cpulist_filename = fmt::format("/sys/devices/system/node/node{}/cpulist", numa_node);
    FILE*       cpulist_file     = fopen(cpulist_filename.c_str(), "rb");
    if (cpulist_file == nullptr)
    {
        LOG_WARN(HCL_OFI, "unable to open cpulist file {}", cpulist_filename);
        return cpuid;
    }
    char cpulist[32] = {0};
    int  result      = fread(cpulist, 1, sizeof(cpulist), cpulist_file);
    fclose(cpulist_file);
    if (result <= 0)
    {
        LOG_WARN(HCL_OFI, "unable to read cpulist file");
        return cpuid;
    }

    try
    {
        std::string cpulist_str {cpulist};
        size_t      pos = cpulist_str.find_first_of('-');
        cpuid           = std::stoi(cpulist_str.substr(0, pos));
    }
    catch (const std::exception& e)
    {
    }

    return cpuid;
}

static PCIE_Device get_pci_info(const std::string& pci_addr)
{
    const PCIE_Device pcie_dev = {pci_addr.substr(0, 7),
                                  get_numa_node(pci_addr),
                                  get_pci_switch_addr_prefix(pci_addr),
                                  get_full_pci_addr(pci_addr)};
    return pcie_dev;
}

static provider_priority get_provider_priority(const std::string& provider_name)
{
    static const std::unordered_map<std::string, provider_priority> priorities {{"tcp", provider_priority::TCP},
                                                                                {"efa", provider_priority::EFA},
                                                                                {"verbs", provider_priority::VERBS}};

    for (const auto& [name, type] : priorities)
    {
        if (provider_name.find(name) != std::string::npos)
        {
            return type;
        }
    }
    return provider_priority::NONE;
}

static int in_list(const char* const item, const char* const list)
{
    int   ret   = 0;
    char* token = NULL;

    char* list_temp = strdup(list);
    if (list_temp == NULL && list != NULL)
    {
        LOG_ERR(HCL_OFI, "Unable to duplicate list");
        ret = hcclLibfabricError;
        free(list_temp);
        return ret;
    }

    token = strtok((char*)list_temp, ",");
    while (token)
    {
        if (!strncmp(item, token, strlen(token)))
        {
            ret = hcclLibfabricError;
            free(list_temp);
            return ret;
        }
        token = strtok(NULL, ",");
    }

    free(list_temp);
    return ret;
}

bool ofi_t::exclude_tcp_provider(const char* const               name,
                                 const uint32_t                  addr_format,
                                 const uint64_t                  mem_tag_format,
                                 const uint64_t                  expected_mem_tag_format,
                                 const std::vector<std::string>& unique_interfaces)
{
    char* tcp_if_exclude_list = hl_ofi_exclude_tcp_if();

    auto                     desired_tcp_if = get_desired_tcp_if_from_env_var();
    std::vector<std::string> parsed_ifs_prefix_list;
    parse_user_tcp_ifs(desired_tcp_if, parsed_ifs_prefix_list);

    if (in_list(name, tcp_if_exclude_list))
    {
        LOG_HCL_DEBUG(HCL_OFI, "Filtering out provider {} due to explicit exclusion request", std::string(name));
        return true;
    }
    else if (addr_format == FI_SOCKADDR_IN6)
    {
        // Currently ipv6 formats are not supported.
        LOG_HCL_DEBUG(HCL_OFI,
                      "Filtering out provider {} due to addr_format mismatch: Expected FI_SOCKADDR_IN, received {}",
                      ofi_plugin->w_fi_tostr(&addr_format, FI_TYPE_ADDR_FORMAT),
                      std::string(name));
        return true;
    }
    else if (mem_tag_format != expected_mem_tag_format)
    {
        // This is here to support libfabric 1.11.0 - https://github.com/ofiwg/libfabric/issues/6126 was fixed in 1.14.0
        // but not 1.11.0.
        LOG_HCL_DEBUG(HCL_OFI,
                      "Filtering out domain {} due to mem_tag_format mismatch: Expected {}, received {}",
                      std::string(name),
                      int_to_hex(expected_mem_tag_format),
                      int_to_hex(mem_tag_format));
        return true;
    }
    else if (!desired_tcp_if.empty() && !match_tcp_if_pattern(name, parsed_ifs_prefix_list))
    {
        // remove all nics that the user didn't ask for
        LOG_HCL_DEBUG(HCL_OFI, "Filtering out provider {} due to explicit exclusion request", std::string(name));
        return true;
    }
    else if (std::contains(unique_interfaces, name))
    {
        LOG_HCL_DEBUG(HCL_OFI, "Filtering out provider {} as it was already detected", std::string(name));
        return true;
    }
    return false;
}

bool ofi_t::exclude_verbs_provider(const char* const name,
                                   const uint32_t    addr_format,
                                   const uint64_t    mem_tag_format,
                                   const uint64_t    expected_mem_tag_format)
{
    if (addr_format != FI_SOCKADDR_IN)
    {
        LOG_HCL_DEBUG(HCL_OFI,
                      "Filtering out domain {} due to addr_format mismatch: Expected FI_SOCKADDR_IN, received {}",
                      std::string(name),
                      ofi_plugin->w_fi_tostr(&addr_format, FI_TYPE_ADDR_FORMAT));
        return true;
    }
    else if (mem_tag_format != expected_mem_tag_format)
    {
        LOG_HCL_DEBUG(HCL_OFI,
                      "Filtering out domain {} due to mem_tag_format mismatch: Expected {}, received {}",
                      std::string(name),
                      int_to_hex(expected_mem_tag_format),
                      int_to_hex(mem_tag_format));
        return true;
    }
    return false;
}

static void get_hints(struct fi_info* hints, bool gaudi_direct)
{
    if (gaudi_direct)
    {
        hints->caps                 = FI_MSG | FI_HMEM;
        hints->domain_attr->mr_mode = FI_MR_LOCAL | FI_MR_HMEM;
    }
    else
    {
        hints->caps                 = FI_MSG;
        hints->domain_attr->mr_mode = FI_MR_LOCAL;
    }

    if (GCFG_HCL_HNIC_RDM.value())
    {
        hints->caps |= FI_TAGGED;
        hints->ep_attr->type = FI_EP_RDM;
    }
    else
    {
        hints->ep_attr->type = FI_EP_MSG;
    }

    // Set MR mode bits to indicate FI_MR_BASIC registration with local memory buffers
    // Will need to change if device memory can be accessed
    hints->domain_attr->mr_mode |= FI_MR_VIRT_ADDR | FI_MR_ALLOCATED | FI_MR_PROV_KEY;

    hints->mode = FI_CONTEXT;

    hints->domain_attr->control_progress = FI_PROGRESS_AUTO;
    hints->domain_attr->data_progress    = FI_PROGRESS_AUTO;

    hints->tx_attr->msg_order = FI_ORDER_SAS;
    hints->rx_attr->msg_order = FI_ORDER_SAS;
}

static int run_fi_getinfo(struct fi_info** providers, bool gaudi_direct)
{
    struct fi_info* hints = ofi_plugin->w_fi_allocinfo();
    if (hints == NULL)
    {
        LOG_ERR(HCL_OFI, "Unable to allocate hints fi_info structure");
        return hcclLibfabricError;
    }

    get_hints(hints, gaudi_direct);
    int rc = ofi_plugin->w_fi_getinfo(ofi_version, nullptr, nullptr, 0ULL, hints, providers);
    ofi_plugin->w_fi_freeinfo(hints);

    if (rc != 0)
    {
        if (*providers)
        {
            ofi_plugin->w_fi_freeinfo(*providers);
        }

        if (rc == -FI_ENODATA)
        {
            LOG_WARN(HCL_OFI, "Could not find any matching provider. Gaudi-direct was set to: {}", gaudi_direct);
        }

        LOG_ERR(HCL_OFI,
                "fi_getinfo() failed with rc {}, {}, gaudi_direct_mode: [{}]",
                rc,
                ofi_plugin->w_fi_strerror(-rc),
                gaudi_direct);
        rc = hcclLibfabricError;
    }

    return rc;
}

ofi_t::ofi_t(int hw_module_id) : m_hw_module_id(hw_module_id), m_components()
{
    m_nOFIDevices    = 0;
    m_ofi_lock       = PTHREAD_MUTEX_INITIALIZER;
    m_is_initialized = false;
}

ofi_t::~ofi_t()
{
    // TODO: for some reason this causes a segfault on DL1. we should investigate.
    // ofi_plugin->w_fi_freeinfo(m_fi_getinfo_result);
}

bool ofi_t::checkDMABUFSupport()
{
    bool    isSupported = false;
    char*   line        = NULL;
    size_t  line_size   = 0;
    ssize_t bytes;
    FILE*   kallsyms_fd;

    kallsyms_fd = fopen("/proc/kallsyms", "r");
    if (!kallsyms_fd)
    {
        LOG_HCL_ERR(HCL_OFI, "Could not check Linux kernel symbols for dmabuf support.");
    }

    while ((bytes = getline(&line, &line_size, kallsyms_fd)) != -1)
    {
        if (strstr(line, "ib_umem_dmabuf_get"))
        {
            isSupported = true;
            break;
        }
    }

    free(line);
    fclose(kallsyms_fd);
    return isSupported;
}

int ofi_t::init(int device_fd)
{
    int         ret;
    int         rc;
    bool        gaudi_direct_supported = false;
    std::string failure_str            = "";

    //
    // RDMAV_FORK_SAFE environment variable makes the rdma-core library fork-safe.
    // This significantly increases cost of memory registration when huge pages are
    // enabled.
    //
    // To prevent data corruption, the EFA provider registers an atfork handler
    // which will abort the process whenever it believes rdma-core is not fork-safe.
    //
    // Enable this environment variable as a trade-off... apps would be able to use
    // fork, but memory registration costs will be high.
    //
    if (!getenv("RDMAV_FORK_SAFE"))
    {
        LOG_HCL_DEBUG(HCL_OFI, "Setting RDMA_FORK_SAFE environment variable to 1");
        rc = setenv("RDMAV_FORK_SAFE", "1", 1);
        if (rc != 0)
        {
            LOG_HCL_ERR(HCL_OFI, "Unable to set RDMAV_FORK_SAFE, continuing without");
        }
    }

    if (GCFG_HCCL_GAUDI_DIRECT.value())
    {
        // Check libfabric version
        uint32_t fabric_version = ofi_plugin->w_fi_version();
        if (FI_MAJOR(fabric_version) > REQUIRED_LIBFABRIC_MAJOR ||
            (FI_MAJOR(fabric_version) == REQUIRED_LIBFABRIC_MAJOR &&
             FI_MINOR(fabric_version) >= REQUIRED_LIBFABRIC_MINOR))
        {
            LOG_HCL_DEBUG(HCL_OFI,
                          "libfabric version is {}.{}, attempting to use gaudi-direct.",
                          FI_MAJOR(fabric_version),
                          FI_MINOR(fabric_version));

            gaudi_direct_supported = checkDMABUFSupport();

            // Check kernel version
            struct utsname uname_buf;
            if (uname(&uname_buf) < 0)
            {
                LOG_HCL_ERR(HCL_OFI, "Could not check Linux kernel version for libfabric gaudi-direct.");
            }
            else
            {
                std::string       major, minor;
                char              delimiter = '.';
                std::stringstream ss(uname_buf.release);
                std::getline(ss, major, delimiter);
                std::getline(ss, minor, delimiter);

                if (!gaudi_direct_supported)
                {
                    LOG_HCL_DEBUG(HCL_OFI,
                                  "Didn't find dmabuf symbols in kernel symbols file, Kernel version is {}.{}, "
                                  "gaudi-direct is not supported.",
                                  major,
                                  minor);
                    failure_str = "Gaudi-direct not supported due to missing symbols in kernel version " +
                                  std::string(major) + "." + std::string(minor) + ". Try using kernel version " +
                                  std::to_string(REQUIRED_KERNEL_MAJOR) + "." + std::to_string(REQUIRED_KERNEL_MINOR);
                }
                else
                {
                    LOG_HCL_DEBUG(HCL_OFI,
                                  "Found dmabuf symbols in kernel symbols file, Kernel version is {}.{}, attempting to "
                                  "use gaudi-direct.",
                                  major,
                                  minor);
                }
            }
        }
        else
        {
            LOG_HCL_DEBUG(HCL_OFI,
                          "libfabric version is {}.{}, gaudi-direct is not supported.",
                          FI_MAJOR(fabric_version),
                          FI_MINOR(fabric_version));
            failure_str =
                "Gaudi-direct not supported due to libfabric version: " + std::to_string(FI_MAJOR(fabric_version)) +
                "." + std::to_string(FI_MINOR(fabric_version)) +
                " while the required version is: " + std::to_string(REQUIRED_LIBFABRIC_MAJOR) + "." +
                std::to_string(REQUIRED_LIBFABRIC_MINOR);
        }

        // If gaudi-direct was explicitly requested by user, but one of the following conditions was met:
        // 1. libfabric version does not support gaudi-direct
        // 2. kernel version does not support gaudi-direct
        // We cannot continue in any other mode and have to exit.
        if (!gaudi_direct_supported)
        {
            if (GCFG_HCCL_GAUDI_DIRECT.isSetFromUserConfig())
            {
                LOG_HCL_ERR(HCL_OFI, "{}", failure_str);
                return hcclLibfabricError;
            }
            else
            {
                GCFG_HCCL_GAUDI_DIRECT.setValue(false);
            }
        }
    }

    m_gaudi_pci_dev = get_pci_info(get_gaudi_pci_ep_addr(device_fd));

    // If gaudi_direct_supported = true, attempt to get provider that supports gaudi-direct.
    // Otherwise, get provider without gaudi-direct.
    ret = get_ofi_provider(device_fd, gaudi_direct_supported);
    if ((ret != hcclSuccess) || (m_providers.size() == 0))
    {
        // Try to get provider again if all the bellow conditions met"
        // 1. Gaudi-direct was not requested by user
        // 2. Previously we tried to get gaudi-direct provider and failed
        if (!GCFG_HCCL_GAUDI_DIRECT.isSetFromUserConfig() && gaudi_direct_supported)
        {
            LOG_HCL_DEBUG(HCL_OFI, "Gaudi-direct was not requested by user. Attempt to use OFI without gaudi-direct.");
            ret = get_ofi_provider(device_fd, false);
            if ((ret != hcclSuccess) || (m_providers.size() == 0))
            {
                LOG_HCL_ERR(HCL_OFI, "Get OFI provider failed");
                return hcclLibfabricError;
            }
        }
        else
        {
            LOG_HCL_ERR(HCL_OFI, "Get OFI provider failed");
            return hcclLibfabricError;
        }
    }

    m_nOFIDevices = m_providers.size();

    struct fi_info* info = *m_providers.begin();

    int index = 1;
    for (struct fi_info* currInfo : m_providers)
    {
        LOG_HCL_TRACE(HCL_OFI,
                      "Provider name: {} number: {}/{} Provider info: {}",
                      currInfo->fabric_attr->prov_name,
                      index++,
                      nOFIDevices(),
                      ofi_plugin->w_fi_tostr(currInfo, FI_TYPE_INFO));
    }

    if (info->domain_attr->mr_mode & FI_MR_LOCAL)
    {
        // currently, only gaudi-direct and verbs provider require MR_LOCAL
        if (isGaudiDirect() || isVerbs())
        {
            LOG_HCL_DEBUG(HCL_OFI,
                          "Provider {} requires registration of local memory buffers",
                          info->fabric_attr->prov_name);
            s_mrLocal = true;
        }
    }

    if (info->domain_attr->mr_mode & FI_MR_HMEM)
    {
        LOG_HCL_DEBUG(HCL_OFI,
                      "Provider {} requires registration of device memory buffers",
                      info->fabric_attr->prov_name);
        s_hmemMR = true;
    }

    try
    {
        m_components.resize(m_nOFIDevices, NULL);
    }
    catch (...)
    {
        LOG_HCL_DEBUG(HCL_OFI, "Could not allocate memory to hold all components");
        return hcclLibfabricError;
    }

    return ret;
}

int ofi_t::listen(int ofiDevice, void* handle, listenComm_t** listenComm)
{
    int      ret;
    uint64_t tag = 0;

    if (OFI_UNLIKELY(ofiDevice < 0 || ofiDevice >= m_nOFIDevices))
    {
        LOG_HCL_ERR(HCL_OFI,
                    "Incorrect OFI Device ID {} provided. Correct values are from 0 to {}.",
                    ofiDevice,
                    m_nOFIDevices - 1);
        return hcclLibfabricError;
    }

    pthread_mutex_lock(&m_ofi_lock);
    ret = acquireOfiComponent(ofiDevice);
    if (ret)
    {
        pthread_mutex_unlock(&m_ofi_lock);
        return hcclLibfabricError;
    }

    assert(m_components[ofiDevice] != NULL);
    ret = m_components[ofiDevice]->next_tag(&tag);
    pthread_mutex_unlock(&m_ofi_lock);
    if (ret)
    {
        return hcclLibfabricError;
    }

    ret = m_components[ofiDevice]->listen(tag, handle, listenComm);
    if (ret)
    {
        return hcclLibfabricError;
    }

    return hcclSuccess;
}

int ofi_t::connect(int ofiDevice, void* handle, ofiComm_t** ofiComm, void* localAddr)
{
    int ret;

    if (OFI_UNLIKELY(ofiDevice < 0 || ofiDevice >= m_nOFIDevices))
    {
        LOG_HCL_ERR(HCL_OFI,
                    "Incorrect OFI Device ID {} provided. Correct values are from 0 to {}",
                    ofiDevice,
                    m_nOFIDevices - 1);
        return hcclLibfabricError;
    }

    pthread_mutex_lock(&m_ofi_lock);
    ret = acquireOfiComponent(ofiDevice);
    pthread_mutex_unlock(&m_ofi_lock);
    if (ret)
    {
        return hcclLibfabricError;
    }

    ret = m_components[ofiDevice]->connect(handle, ofiComm, localAddr);
    if (ret)
    {
        return hcclLibfabricError;
    }

    return hcclSuccess;
}

int ofi_t::accept(listenComm_t* listenComm, ofiComm_t** ofiComm)
{
    int ret;

    pthread_mutex_lock(&m_ofi_lock);
    ret = acquireOfiComponent(listenComm->dev);
    pthread_mutex_unlock(&m_ofi_lock);
    if (ret)
    {
        return hcclLibfabricError;
    }

    ret = m_components[listenComm->dev]->accept(listenComm, ofiComm);
    if (ret)
    {
        return hcclLibfabricError;
    }
    return hcclSuccess;
}

int ofi_t::isend(ofiComm_t*             ofiComm,
                 void*                  data,
                 size_t                 size,
                 fid_mr*                mHandle,
                 ofi_req_t**            request,
                 OfiCompCallbackParams& compParams)
{
    int ret;

    if (OFI_UNLIKELY(ofiComm == NULL))
    {
        LOG_HCL_ERR(HCL_OFI, "Invalid ofiComm");
        return hcclLibfabricError;
    }

    if (OFI_UNLIKELY(ofiComm->num_inflight_sends == OFI_MAX_REQUESTS))
    {
        LOG_HCL_ERR(HCL_OFI, "Can't support more than {} inflight requests", OFI_MAX_REQUESTS);
        return hcclLibfabricError;
    }

    ret = m_components[ofiComm->dev]->isend(ofiComm, data, size, mHandle, request, compParams);
    if (ret)
    {
        return hcclLibfabricError;
    }

    return hcclSuccess;
}

int ofi_t::irecv(ofiComm_t*             ofiComm,
                 void*                  data,
                 size_t                 size,
                 fid_mr*                mHandle,
                 ofi_req_t**            request,
                 OfiCompCallbackParams& compParams)
{
    int ret;

    if (OFI_UNLIKELY(ofiComm == NULL))
    {
        LOG_HCL_ERR(HCL_OFI, "Invalid ofiComm");
        return hcclLibfabricError;
    }

    if (OFI_UNLIKELY(ofiComm->num_inflight_recvs == OFI_MAX_REQUESTS))
    {
        LOG_HCL_ERR(HCL_OFI, "Can't support more than {} inflight requests", OFI_MAX_REQUESTS);
        return hcclLibfabricError;
    }

    ret = m_components[ofiComm->dev]->irecv(ofiComm, data, size, mHandle, request, compParams);
    if (ret)
    {
        return hcclLibfabricError;
    }

    return hcclSuccess;
}

int ofi_t::test(ofi_req_t* request, int* done, size_t* size)
{
    int ret;

    if (OFI_UNLIKELY(request == NULL))
    {
        LOG_HCL_ERR(HCL_OFI, "request value isn't valid");
        return hcclLibfabricError;
    }

    ret = m_components[request->ofiDevice]->test(request, done, size);
    if (ret)
    {
        return hcclLibfabricError;
    }

    return hcclSuccess;
}

int ofi_t::close(ofiComm_t* ofiComm)
{
    if (OFI_UNLIKELY(ofiComm == NULL))
    {
        LOG_HCL_ERR(HCL_OFI, "ofiComm value isn't valid");
        return hcclLibfabricError;
    }

    int ofiDevice = ofiComm->dev;
    m_components[ofiDevice]->close(ofiComm);

    pthread_mutex_lock(&m_ofi_lock);
    releaseOfiComponent(ofiDevice);
    pthread_mutex_unlock(&m_ofi_lock);

    return hcclSuccess;
}

int ofi_t::close(listenComm_t* listenComm)
{
    if (OFI_UNLIKELY(listenComm == NULL))
    {
        LOG_HCL_ERR(HCL_OFI, "listenComm value isn't valid");
        return hcclLibfabricError;
    }

    int ofiDevice = listenComm->dev;
    m_components[ofiDevice]->close(listenComm);

    pthread_mutex_lock(&m_ofi_lock);
    releaseOfiComponent(ofiDevice);
    pthread_mutex_unlock(&m_ofi_lock);

    return hcclSuccess;
}

int ofi_t::initOfiComponent(int ofiDevice)
{
    int             ret;
    struct fi_info* prov = NULL;
    prov                 = get_nic_info(ofiDevice);
    if (OFI_UNLIKELY(prov == NULL))
    {
        LOG_HCL_ERR(HCL_OFI, "Could not extract provider information for given NIC ID {}", ofiDevice);
        if (m_components[ofiDevice] != NULL)
        {
            delete m_components[ofiDevice];
            m_components[ofiDevice] = NULL;
        }
        return hcclLibfabricError;
    }

    int cpuid     = -1;
    int numa_node = m_gaudi_pci_dev.numa_node;
    if (numa_node >= 0)
    {
        cpuid = get_cpuid_in_numa(numa_node);
    }

    if (GCFG_HCL_HNIC_RDM.value())
    {
        m_components[ofiDevice] = new ofi_rdm_component_t(ofiDevice, m_hw_module_id, prov, cpuid);
    }
    else
    {
        m_components[ofiDevice] = new ofi_msg_component_t(ofiDevice, m_hw_module_id, prov, cpuid);
    }

    if (OFI_UNLIKELY(m_components[ofiDevice] == NULL))
    {
        LOG_HCL_ERR(HCL_OFI, "Memory allocation failed for OFI component");
        if (m_components[ofiDevice] != NULL)
        {
            delete m_components[ofiDevice];
            m_components[ofiDevice] = NULL;
        }
        return hcclLibfabricError;
    }

    ret = m_components[ofiDevice]->create_component();
    if (ret != 0)
    {
        if (m_components[ofiDevice] != NULL)
        {
            delete m_components[ofiDevice];
            m_components[ofiDevice] = NULL;
        }
        return hcclLibfabricError;
    }

    if (prov->nic == 0)
    {
        LOG_HCL_DEBUG(HCL_OFI, "OFI component #{} is created.", ofiDevice);
    }
    else
    {
        LOG_HCL_INFO(HCL_OFI,
                     "OFI component id={} is created. device name {}, device id {}",
                     ofiDevice,
                     prov->nic->device_attr->name,
                     prov->nic->device_attr->device_id);
    }
    m_is_initialized = true;
    return ret;
}

int ofi_t::acquireOfiComponent(int ofiDevice)
{
    if (m_components[ofiDevice] == NULL)
    {
        VERIFY(initOfiComponent(ofiDevice) == hcclSuccess, "initializing OFI component failed.");
    }
    else
    {
        m_components[ofiDevice]->inc_refcnt();
    }

    return hcclSuccess;
}

void ofi_t::releaseOfiComponent(int ofiDevice)
{
    if (m_components[ofiDevice] == NULL)
    {
        LOG_HCL_ERR(HCL_OFI, "OFI component already deleted (or never created) for component {}", ofiDevice);
        return;
    }

    if (m_components[ofiDevice]->dec_refcnt() == 0)
    {
        delete m_components[ofiDevice];
        m_components[ofiDevice] = NULL;

        LOG_HCL_DEBUG(HCL_OFI, "OFI component #{} is released", ofiDevice);
    }
    else
    {
        LOG_HCL_TRACE(HCL_OFI,
                      "OFI component #{} has {} reference(s)",
                      ofiDevice,
                      m_components[ofiDevice]->get_refcnt());
    }
}

int ofi_t::get_ofi_provider(int device_fd, bool gaudi_direct)
{
    int rc = run_fi_getinfo(&m_fi_getinfo_result, gaudi_direct);
    if (rc != 0) return rc;

    std::vector<struct fi_info*>    providers;
    std::vector<std::string>        unique_tcp_interfaces;
    std::unordered_set<std::string> unique_domain_names;
    uint64_t                        expected_mem_tag_format = 0;
    provider_priority               provider_priority       = provider_priority::NONE;

    LOG_HCL_DEBUG(HCL_OFI,
                  "gaudi pci address = {}, numa node = {}",
                  m_gaudi_pci_dev.full_path,
                  m_gaudi_pci_dev.numa_node);

    for (struct fi_info* curr = m_fi_getinfo_result; curr != nullptr; curr = curr->next)
    {
        std::string provider_name {curr->fabric_attr->prov_name};
        LOG_HCL_CONTEXT_DEBUG(HCL_OFI,
                              "Found provider: {}, checking if it's a match for what we require...",
                              provider_name);

        if (expected_mem_tag_format == 0)
        {
            expected_mem_tag_format = curr->ep_attr->mem_tag_format;
        }

        const auto current_provider_priority = get_provider_priority(provider_name);
        if (provider_priority::NONE == current_provider_priority)
        {
            LOG_HCL_DEBUG(HCL_OFI, "Provider {} is not supported, skipping...", provider_name);
            continue;
        }
        else if (provider_priority > current_provider_priority)
        {
            LOG_HCL_DEBUG(HCL_OFI, "Already found a better-prioritized provider than {}, skipping...", provider_name);
            continue;
        }

        if (provider_priority::TCP == current_provider_priority)
        {
            if (exclude_tcp_provider(curr->domain_attr->name,
                                     curr->addr_format,
                                     curr->ep_attr->mem_tag_format,
                                     expected_mem_tag_format,
                                     unique_tcp_interfaces))

            {
                continue;
            }
            unique_tcp_interfaces.push_back(curr->domain_attr->name);
        }
        else if (provider_priority::VERBS == current_provider_priority)
        {
            if (exclude_verbs_provider(curr->domain_attr->name,
                                       curr->addr_format,
                                       curr->ep_attr->mem_tag_format,
                                       expected_mem_tag_format))
            {
                continue;
            }

            const PCIE_Device verbs_pcie_dev = get_pci_info(get_verbs_pci_ep_addr(curr->domain_attr->name));
            LOG_HCL_DEBUG(HCL_OFI,
                          "current verbs pci addr: {}, current verbs numa: {}",
                          verbs_pcie_dev.full_path,
                          verbs_pcie_dev.numa_node);
        }
        else if (provider_priority::EFA == current_provider_priority)
        {
            PCIE_Device efa_pcie_dev = get_pci_info(get_efa_pci_ep_addr(curr->nic->bus_attr));
            LOG_HCL_DEBUG(HCL_OFI,
                          "current verbs pci addr: {}, current efa numa: {}",
                          efa_pcie_dev.full_path,
                          std::to_string(efa_pcie_dev.numa_node));
        }

        if (current_provider_priority > provider_priority)
        {
            // A better provider type was found
            providers.clear();
            provider_priority = current_provider_priority;
        }

        // Same provider type as previous one, check for pci addr match
        providers.push_back(curr);

        LOG_HCL_DEBUG(HCL_OFI,
                      "We have a match! Adding provider {}, domain {}",
                      provider_name,
                      curr->domain_attr->name);
        unique_domain_names.emplace(curr->domain_attr->name);
    }

    if (providers.empty())
    {
        LOG_HCL_WARN(HCL_OFI, "Found no fitting provider");
        return hcclLibfabricError;
    }

    s_verbs                        = (provider_priority == provider_priority::VERBS);
    const std::string providerName = (*providers.cbegin())->fabric_attr->prov_name;

    s_gaudiDirect = gaudi_direct;
    if (s_gaudiDirect)
    {
        LOG_HCL_INFO(HCL_OFI, "Gaudi-direct is enabled, provider {}.", providerName);
    }

    for (const auto& domain : unique_domain_names)
    {
        bool foundFirstMatch = false;
        for (std::vector<struct fi_info*>::iterator itr = providers.begin(); itr != providers.end();)
        {
            if ((*itr)->nic == 0)
            {
                itr++;
                continue;
            }
            char* devName = (*itr)->nic->device_attr->name;
            // found domain name match
            if (domain == devName)
            {
                // only one occurrence of domain name should be left in the list
                if (foundFirstMatch)
                {
                    itr = providers.erase(itr);
                }
                else
                {
                    foundFirstMatch = true;
                    itr++;
                }
            }
            else
            {
                itr++;
            }
        }
    }

    const std::string accelPath   = getHLDevice(device_fd);
    const std::string accel       = accelPath.substr(accelPath.find_last_of("/") + 1);
    std::string       description = "";
    m_ofi_device                  = 0;  // This is always the first one because there is only one in m_providers.
    if (isVerbs())
    {
        const auto bestProvider = hl_topo::getBestProvider(providers, accel);
        m_providers             = {providers[std::get<size_t>(bestProvider)]};  // Only the selected provider saved
        description             = fmt::format(" connected via {}", std::get<std::string>(bestProvider));
    }
    else
    {
        const auto providerIndex = m_hw_module_id % providers.size();
        m_providers              = {providers[providerIndex]};
    }

    {
        size_t num_provs = providers.size();
        LOG_HCL_CONTEXT_INFO(HCL_OFI,
                             "Finished scanning provider list, found {} suitable {} provider{},{} for Gaudi {}",
                             num_provs,
                             providerName,
                             num_provs > 1 ? "s" : "",
                             description,
                             m_gaudi_pci_dev.full_path);

        int index = 1;
        for (struct fi_info* currInfo : providers)
        {
            PCIE_Device pcie_dev;
            size_t      active_mtu = 0;
            if (isVerbs())
            {
                pcie_dev   = get_pci_info(get_verbs_pci_ep_addr(currInfo->domain_attr->name));
                active_mtu = currInfo->nic->link_attr->mtu;
            }
            LOG_HCL_INFO(HCL_OFI,
                         "{}/{}: {} {}{}{}",
                         index++,
                         num_provs,
                         currInfo->domain_attr->name,
                         pcie_dev.full_path,
                         isVerbs() ? " active_mtu=" + std::to_string(active_mtu) : "",
                         ((currInfo == m_providers[m_ofi_device]) ? " (Selected)" : ""));
        }
    }

    return hcclSuccess;
}

struct fi_info* ofi_t::get_nic_info(int ofiDevice)
{
    int dev_idx = 0;
    for (struct fi_info* info : m_providers)
    {
        if (dev_idx++ == ofiDevice) return info;
    }
    return nullptr;
}

ofi_component_t* ofi_t::getOfiComponent(int ofiDevice)
{
    if (m_components[ofiDevice] == NULL)
    {
        int ret = initOfiComponent(ofiDevice);
        VERIFY(ret == hcclSuccess, "Failed to get ofi component");
    }

    return m_components[ofiDevice];
}
