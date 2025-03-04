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
#include "hccl_helpers.h"  // For LOG_WARN

#define CTRL_BUF_SIZE (128)

#define DEVICE_ID (0)

enum class CommOp : uint8_t
{
    SEND = 0,
    RECV
};

#ifndef container_of
#define container_of(ptr, type, field) ((type*)((char*)ptr - offsetof(type, field)))
#endif

inline int ofiCommOp(const CommOp           op,
                     ofiComm_t*             ofiComm,
                     void*                  data,
                     size_t                 size,
                     ofi_req_t**            req,
                     ofi_t*                 g_ofi,
                     OfiCompCallbackParams& compParams)
{
    int        ret     = hcclUninitialized;
    ofi_req_t* request = NULL;

    do
    {
        switch (op)
        {
            case CommOp::SEND:
                ret = g_ofi->isend(ofiComm, data, size, &request, compParams);
                break;
            case CommOp::RECV:
                ret = g_ofi->irecv(ofiComm, data, size, &request, compParams);
                break;
            default:
                VERIFY(false, "Unknown ofi operation.");
        }
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
