#include "ofi_plugin.h"

#include "libfabric/hl_ofi.h"            // for ofi_t
#include <dlfcn.h>                       // for dlclose, dlopen, dlsym, RTLD...
#include <optional>                      // for std::optional
#include "hcl_log_manager.h"             // for LOG_ERR, LOG_DEBUG, LOG_INFO
#include "hcl_utils.h"                   // for VERIFY
#include "hccl_ofi_wrapper_interface.h"  // for ofi_plugin_interface
#include "so.h"                          // for SharedObject

static std::optional<SharedObject> handle_;

OfiPlugin::OfiPlugin(int fd, int hw_module_id)
{
    VERIFY(initializeOFIPluginIfNeeded(), "Failed to get ofi_plugin");

    p_ofi = std::make_unique<ofi_t>(fd, hw_module_id);

    VERIFY(!p_ofi->init(), "Libfabric init failed");
    VERIFY(p_ofi->nOFIDevices() != 0, "No available OFI devices");
}

bool OfiPlugin::initializeOFIPluginIfNeeded()
{
    if (ofi_plugin != nullptr)
    {
        return true;
    }

    LOG_INFO(HCL, "Initializing ofi_plugin.");
    try
    {
        handle_.emplace("libhccl_ofi_wrapper.so", RTLD_LAZY);
    }
    catch (const hcl::HclException&)
    {
        LOG_WARN(HCL, "Error loading OFI wrapper.");
        return false;
    }
    p_get_version        = handle_->symbol<double (*)()>("get_version");
    const double version = (*p_get_version)();
    if (static_cast<int>(version) == 0)
    {
        LOG_ERR(HCL, "Error in getting OFI wrapper version.");
        return false;
    }
    if (version >= get_wrapper_required_version())
    {
        LOG_INFO_F(HCL, "OFI wrapper version is: {}", version);
    }

    p_create   = handle_->symbol<ofi_plugin_interface_handle (*)()>("create_ofi_plugin_handle");
    ofi_plugin = (*p_create)();
    if (ofi_plugin == nullptr)
    {
        LOG_ERR(HCL, "Error in creating ofi_wrapper pointer");
        return false;
    }
    return true;
}

ofi_plugin_interface_handle (*OfiPlugin::p_create)() = NULL;
double (*OfiPlugin::p_get_version)()                 = NULL;
