#include <stdbool.h>
#include <dlfcn.h>
#include "hcl_ibv_loader.h"
#include "hcl_utils.h"

#define _DLSYM_(handle, fname)                                                                                         \
    fname = (fname##_fn)dlsym(handle, #fname);                                                                         \
    if (!fname) return false

#define LIBFUNC(fname) _DLSYM_(hlib_handle, fname)

#define __2str(x) #x
#define IBVFUNC(fname)                                                                                                 \
    fname = (fname##_fn)dlsym(hlib_handle, __2str(hl##fname));                                                         \
    if (!fname) return false

ibv_lib_t g_ldr;

void* load_rdma_lib()
{
    // If RDMA_CORE_LIB env var exist use it
    // Else: try default load from LD_LIBARY_PATH
    // Else: fixed path (need to verify): /opt/habanalabs/rdma-core/src/build/lib

    /* clear dlerror */
    dlerror();

    std::string so_name = getEnvStr("RDMA_CORE_LIB");
    if (so_name != "")
    {
        so_name += "/";
    }

    so_name += "libhbl.so";

    void* handle = dlopen(so_name.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (handle == nullptr)
    {
        so_name = GCFG_HCL_RDMA_DEFAULT_PATH.value() + "/libhbl.so";
        handle  = dlopen(so_name.c_str(), RTLD_LOCAL | RTLD_NOW);
    }

    return handle;
}

bool ibv_lib_t::load()
{
    if (loaded)
    {
        return true;
    }

    hlib_handle = load_rdma_lib();
    if (!hlib_handle) return false;

    /* get necessary function pointers */
    LIBFUNC(hbldv_open_device);
    LIBFUNC(hbldv_set_port_ex);
    LIBFUNC(hbldv_create_cq);
    LIBFUNC(hbldv_query_qp);
    LIBFUNC(hbldv_reserve_coll_qps);
    LIBFUNC(hbldv_modify_qp);
    LIBFUNC(hbldv_create_usr_fifo);
    LIBFUNC(hbldv_destroy_usr_fifo);
    LIBFUNC(hbldv_query_port);
    LIBFUNC(hbldv_is_supported);
    LIBFUNC(hbldv_query_device);

    Dl_info info = {};
    dladdr((const void*)hbldv_is_supported, &info);
    LOG_INFO(HCL_IBV, "loaded: {}", info.dli_fname);

    IBVFUNC(ibv_get_device_list);
    IBVFUNC(ibv_get_device_name);
    IBVFUNC(ibv_free_device_list);
    IBVFUNC(ibv_alloc_pd);
    IBVFUNC(ibv_close_device);
    IBVFUNC(ibv_dealloc_pd);
    IBVFUNC(ibv_destroy_cq);
    IBVFUNC(ibv_create_qp);
    IBVFUNC(ibv_modify_qp);
    IBVFUNC(ibv_destroy_qp);
    IBVFUNC(ibv_get_async_event);
    IBVFUNC(ibv_ack_async_event);
    IBVFUNC(ibv_query_qp);
    IBVFUNC(ibv_query_port);
    IBVFUNC(ibv_query_gid);
    IBVFUNC(ibv_query_gid_type);

    info = {};
    dladdr((const void*)ibv_query_gid_type, &info);
    LOG_INFO(HCL_IBV, "loaded: {}", info.dli_fname);

    loaded = true;
    return true;
}

void ibv_lib_t::unload()
{
    if (hlib_handle)
    {
        VERIFY(0 == dlclose(hlib_handle));
    }

    hlib_handle = nullptr;

    loaded = false;
}

ibv_lib_t::~ibv_lib_t()
{
    unload();
}