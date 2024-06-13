#pragma once

#include <iostream>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <cassert>
#include <chrono>
#include "hl_ofi.h"
#include "hl_ofi_component.h"
#include "mr_mapping.h"
#include "hccl_helpers.h"  // For LOG_WARN

#define CTRL_BUF_SIZE (128)

#define DEVICE_ID (0)

inline int post_send(ofiComm_t*             ofiComm,
                     void*                  data,
                     size_t                 size,
                     ofi_req_t**            req,
                     ofi_t*                 g_ofi,
                     OfiCompCallbackParams& compParams,
                     bool                   mr_lookup = true)
{
    int            ret       = hcclSuccess;
    ofi_req_t*     request   = NULL;
    struct fid_mr* mr_handle = NULL;

    if (mr_lookup)
    {
        // Both gaudi-direct and native verbs provider require MR_LOCAL
        if (ofi_t::isMRLocal())
        {
            mr_handle = MRMapping::get_instance().lookup_mr_handle((uint64_t)data, size);

            if (mr_handle == NULL && ofi_t::isVerbs() && !ofi_t::isGaudiDirect())
            {
                LOG_INFO(HCL_OFI,
                         "Post send buffer miss for mr handle of addr: 0x{:x} size: 0x{:x}. Performing host MR "
                         "registration on the fly",
                         (uint64_t)data,
                         size);
                MRMapping::get_instance().mapHostMem(reinterpret_cast<uint64_t>(data),
                                                     size,
                                                     g_ofi->getOfiComponent(ofiComm->dev),
                                                     mr_handle);
            }
            if (mr_handle == NULL)
            {
                LOG_ERR(HCL_OFI, "MR handle not available for addr 0x{:x} size: 0x{:x}", (uint64_t)data, size);
                return hcclLibfabricError;
            }
        }
    }

    do
    {
        ret = g_ofi->isend(ofiComm, data, size, mr_handle, &request, compParams);
    } while (request == NULL);
    if (ret)
    {
        ret     = hcclLibfabricError;
        request = NULL;
        return ret;
    }

    *req = request;

    return ret;
}

inline int post_recv(ofiComm_t*             ofiComm,
                     void*                  data,
                     size_t                 size,
                     ofi_req_t**            req,
                     ofi_t*                 g_ofi,
                     OfiCompCallbackParams& compParams,
                     bool                   mr_lookup = true)
{
    int            ret       = hcclSuccess;
    ofi_req_t*     request   = NULL;
    struct fid_mr* mr_handle = NULL;

    if (mr_lookup)
    {
        // Both gaudi-direct and native verbs provider require MR_LOCAL
        if (ofi_t::isMRLocal())
        {
            mr_handle = MRMapping::get_instance().lookup_mr_handle((uint64_t)data, size);

            if (mr_handle == NULL && ofi_t::isVerbs() && !ofi_t::isGaudiDirect())
            {
                LOG_INFO(HCL_OFI,
                         "Post recv buffer miss for mr handle of addr: 0x{:x} size: 0x{:x}. Performing host MR "
                         "registration on the fly",
                         (uint64_t)data,
                         size);
                MRMapping::get_instance().mapHostMem(reinterpret_cast<uint64_t>(data),
                                                     size,
                                                     g_ofi->getOfiComponent(ofiComm->dev),
                                                     mr_handle);
            }
            if (mr_handle == NULL)
            {
                LOG_ERR(HCL_OFI, "MR handle not available for addr 0x{:x} size: 0x{:x}", (uint64_t)data, size);
                return hcclLibfabricError;
            }
        }
    }

    do
    {
        ret = g_ofi->irecv(ofiComm, data, size, mr_handle, &request, compParams);
    } while (request == NULL);
    if (ret)
    {
        ret     = hcclLibfabricError;
        request = NULL;
        return ret;
    }

    *req = request;
    return ret;
}
