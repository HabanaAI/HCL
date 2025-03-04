#pragma once

#include "hccl_ofi_wrapper_interface.h"  // for ofi_plugin_interface (ptr only)
#include <memory>                        // for std::unique_ptr

class ofi_t;

extern std::unique_ptr<ofi_plugin_interface> ofi_plugin;
typedef void*                                libHandle;

class OfiPlugin
{
public:
    OfiPlugin(int fd, int hw_module_id);
    virtual ~OfiPlugin() = default;

    static bool          initializeOFIPluginIfNeeded();
    static inline double get_wrapper_required_version() { return m_wrapper_required_version; }

    static ofi_plugin_interface_handle (*p_create)();
    static double (*p_get_version)();

    std::unique_ptr<ofi_t> p_ofi;

private:
    static constexpr double m_wrapper_required_version = 1.2;
};
