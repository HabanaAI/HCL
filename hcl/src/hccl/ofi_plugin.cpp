#include "ofi_plugin.h"

#include "libfabric/hl_ofi.h"            // for ofi_t
#include <dlfcn.h>                       // for dlclose, dlopen, dlsym, RTLD...
#include "hcl_log_manager.h"             // for LOG_ERR, LOG_DEBUG, LOG_INFO
#include "hcl_utils.h"                   // for VERIFY
#include "hccl_ofi_wrapper_interface.h"  // for ofi_plugin_interface

static libHandle handle_;

OfiPlugin::OfiPlugin(int fd, int hw_module_id)
{
    VERIFY(initializeOFIPluginIfNeeded(), "Failed to get ofi_plugin");

    p_ofi = new ofi_t(hw_module_id);

    VERIFY(!p_ofi->init(fd), "Libfabric init failed");
    VERIFY(p_ofi->nOFIDevices() != 0, "No available OFI devices");
}

OfiPlugin::~OfiPlugin()
{
    destroy_ofi_plugin();
    delete p_ofi;
}

bool OfiPlugin::initialize_ofi_plugin()
{
    double version = 0;

    handle_ = dlopen("libhccl_ofi_wrapper.so", RTLD_LAZY);
    if (handle_ == nullptr)
    {
        LOG_ERR(HCL, "Error in dlopen for libhccl_ofi_wrapper.so: {}", dlerror());
        return false;
    }

    p_get_version = (double (*)())dlsym(handle_, "get_version");
    if (p_get_version == nullptr)
    {
        LOG_CRITICAL(HCL, "Error in dlsym for get_version. Please update OFI wrapper to use host scale-out.");
        dlclose(handle_);
        return false;
    }
    else
    {
        version = (*p_get_version)();
        if (version == 0)
        {
            LOG_ERR(HCL, "Error in getting OFI wrapper version.");
            dlclose(handle_);
            return false;
        }
        else
        {
            if (version >= get_wrapper_required_version())
            {
                LOG_INFO(HCL, "OFI wrapper version is: {}", version);
            }
            else
            {
                LOG_CRITICAL(HCL,
                             "OFI wrapper version is: {}, while required version is: {}. Please update OFI wrapper to "
                             "enable host scale-out.",
                             version,
                             get_wrapper_required_version());
                dlclose(handle_);
                return false;
            }
        }
    }

    p_create = (ofi_plugin_interface_handle(*)())dlsym(handle_, "create_ofi_plugin_handle");
    if (p_create == nullptr)
    {
        LOG_ERR(HCL, "Error in dlsym for create_ofi_plugin_handle");
        dlclose(handle_);
        return false;
    }
    ofi_plugin = (*p_create)();
    if (ofi_plugin == nullptr)
    {
        LOG_ERR(HCL, "Error in creating ofi_wrapper pointer");
        dlclose(handle_);
        return false;
    }
    return true;
}

void OfiPlugin::destroy_ofi_plugin()
{
    LOG_INFO(HCL, "Destroying ofi_plugin.");
    dlclose(handle_);
}

bool OfiPlugin::initializeOFIPluginIfNeeded()
{
    if (ofi_plugin == nullptr)
    {
        LOG_INFO(HCL, "Initializing ofi_plugin.");
        if (!initialize_ofi_plugin())
        {
            LOG_ERR(HCL, "Failed to initialize ofi_plugin.");
            return false;
        }
    }
    return true;
}

double OfiPlugin::get_wrapper_required_version()
{
    return m_wrapper_required_version;
}

ofi_plugin_interface_handle (*OfiPlugin::p_create)() = NULL;
double (*OfiPlugin::p_get_version)()                 = NULL;
