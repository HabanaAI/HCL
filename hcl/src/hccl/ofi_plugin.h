#pragma once

#include "hccl_ofi_wrapper_interface.h"  // for ofi_plugin_interface (ptr only)

class ofi_t;

extern std::unique_ptr<ofi_plugin_interface> ofi_plugin;
typedef void*                                libHandle;

class OfiPlugin
{
public:
    OfiPlugin(int fd, int hw_module_id);
    ~OfiPlugin();

    static ofi_plugin_interface_handle (*p_create)();
    static double (*p_get_version)();
    static bool   initialize_ofi_plugin();
    static void   destroy_ofi_plugin();
    static bool   initializeOFIPluginIfNeeded();
    static double get_wrapper_required_version();

    ofi_t* p_ofi {nullptr};

private:
    static constexpr double m_wrapper_required_version = 1.1;
};
