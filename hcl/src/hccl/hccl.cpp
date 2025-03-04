/******************************************************************************
 * Copyright (C) 2020-2021 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

#include "hccl.h"  // for HCCL_VERSION_CODE

#include <common/shim_typedefs.h>  // for PFN_ShimFinish, PFN...
#include <dlfcn.h>                 // for dlclose, dlsym, dle...
#include <climits>                 // for INT_MAX
#include <cstdlib>                 // for getenv
#include <cstring>                 // for strcmp
#include <memory>                  // for shared_ptr
#include <string>                  // for string
#include <mutex>                   // for mutx, unique_lock
#include <atomic>
#include <condition_variable>

#include "common/shim_types.h"       // for SHIM_API_HCCL, SHIM...
#include "dfa_defines.hpp"           // for DfaErrorCode, DfaEr...
#include "hccl_api_funcs.h"          // for hccl_functions_poin...
#include "hccl_communicator.h"       // for hccl_communicator
#include "hccl_context.h"            // for hccl_context, g_hcc...
#include "hccl_helpers.h"            // for to_string, to_hccl_...
#include "hccl_internal_defs.h"      // for hcclOpParams, eHCCL...
#include "hccl_types.h"              // for hcclResult_t, hcclC...
#include "hcl_public_streams.h"      // for tdrDetectionFlag
#include "hcl_types.h"               // for HclConfigType, LOOP...
#include "hcl_utils.h"               // for HCL_API_LOG_ENTRY
#include "hccl_api_inc.h"            // for HCCL_* macros
#include "internal/hccl_internal.h"  // for hcclDFA, hcclDestro...
#include "hcl_log_manager.h"         // for LOG_ERR, LOG_DEBUG
#include "hccl_gen2_impl.h"          // for Gen2 hccl impl under HclGen2
#include "hccl_wrapper.h"            // for hccl*_Wrapper
#include "fault_tolerance_inc.h"     // for HLFT.* macros

struct HCL_Request;

// ------------------------------------------------------------------------------------------------
// Global context singleton objects.
//

#define HCCL_API_CALL __attribute__((visibility("default")))

hccl_context hccl_ctx;

// DFA vars
DfaPhase   g_dfaPhase = DfaPhase::NONE;
std::mutex g_dfaMutex;

// Fault tolerance vars
std::atomic<bool> g_faultsCheckStopApi {false};  // To check conditions for faults API stop handling only when necessary
std::atomic<uint32_t>   g_faultsStopAllApi {false};  // To track stop API requests
std::condition_variable g_faultsStopAllApiCv;        // CV to block user API threads
std::mutex              g_faultsStopAllApiMutex;     // Mutex for condition variable

// Called to check if to stop all API's during fault tolerance handling from hccl.cpp and hccl_wrapper.cpp
void checkFaultToleranceStopApi()
{
    LOG_DEBUG(HCL_FAILOVER, "{}: Stop API check", __func__);
    LOG_DEBUG(HCL_API, "{}: Stop API check", __func__);
    std::unique_lock<std::mutex> lk(g_faultsStopAllApiMutex);
    LOG_DEBUG(HCL_FAILOVER, "{}: Before CV wait, g_faultsStopAllApi={}", __func__, g_faultsStopAllApi.load());
    LOG_DEBUG(HCL_API, "{}: Before CV wait, g_faultsStopAllApi={}", __func__, g_faultsStopAllApi.load());
    g_faultsStopAllApiCv.wait(lk,
                              [] { return (g_faultsStopAllApi.load() == 0); }); /* Block if g_faultsStopAllApi != 0 */
    LOG_INFO(HCL_FAILOVER,
             "{}: After CV wait, User API thread is unblocked, g_faultsCheckStopApi={}",
             __func__,
             g_faultsCheckStopApi.load());
    LOG_INFO(HCL_API,
             "{}: After CV wait, User API thread is unblocked, g_faultsCheckStopApi={}",
             __func__,
             g_faultsCheckStopApi.load());
}

static hcclResult_t syncHCLStreamHandle(synStreamHandle stream_handle)
{
    const synStatus    status      = synStreamSyncHCLStreamHandle(stream_handle);
    const hcclResult_t hccl_status = to_hccl_result(status);
    if (hccl_status != hcclSuccess) return hcclInvalidUsage;

    return hcclSuccess;
}

hcclResult_t HCCL_API_CALL hcclGetVersion_Original(int* version)
{
    return hcclGetVersion_Wrapper(version);
}

hcclResult_t HCCL_API_CALL hcclGetUniqueId_Original(hcclUniqueId* uniqueId)
{
    return hcclGetUniqueId_Wrapper(uniqueId);
}

hcclResult_t HCCL_API_CALL hcclCommInitRank_Original(hcclComm_t* comm, int nranks, hcclUniqueId& commId, int rank)
{
    return hcclCommInitRank_Wrapper(comm, nranks, commId, rank);
}

hcclResult_t HCCL_API_CALL hcclCommInitAll_Original(hcclComm_t* comm, int ndev, const int* devlist)
{
    return hcclCommInitAll_Wrapper(comm, ndev, devlist);
}

hcclResult_t HCCL_API_CALL hcclCommFinalize_Original(hcclComm_t comm)
{
    return hcclCommFinalize_Wrapper(comm);
}

static bool hcclIsACcbHalfFull_Original(const unsigned archStreamIdx)
{
    HCCL_TRY
    const bool res = hccl_device()->isACcbHalfFullForDeviceBenchMark(archStreamIdx);
    HCL_API_EXIT(res)
}

static void hcclSetTraceMarker_Original(const synStreamHandle stream_handle, uint32_t val)
{
    hccl_device()->setTraceMarker(stream_handle, val);
}

hcclResult_t HCCL_API_CALL hcclCommDestroy_Original(hcclComm_t comm)
{
    return hcclCommDestroy_Wrapper(comm);
}

hcclResult_t HCCL_API_CALL hcclCommAbort_Original(hcclComm_t comm)
{
    return hcclCommAbort_Wrapper(comm);
}

HCCL_API_CALL const char* hcclGetErrorString_Original(hcclResult_t result)
{
    return hcclGetErrorString_Wrapper(result);
}

hcclResult_t HCCL_API_CALL hcclCommGetAsyncError_Original(hcclComm_t comm, hcclResult_t* asyncError)
{
    return hcclCommGetAsyncError_Wrapper(comm, asyncError);
}

hcclResult_t HCCL_API_CALL hcclCommCount_Original(hcclComm_t comm, int* count)
{
    return hcclCommCount_Wrapper(comm, count);
}

hcclResult_t HCCL_API_CALL hcclCommSynDevice_Original(hcclComm_t comm, int* device)
{
    return hcclCommSynDevice_Wrapper(comm, device);
}

hcclResult_t HCCL_API_CALL hcclCommUserRank_Original(hcclComm_t comm, int* rank)
{
    return hcclCommUserRank_Wrapper(comm, rank);
}

int HCCL_API_CALL hcclLookupDMABuff_Original(uint64_t addr, uint64_t size, int* fd)
{
    return hcclLookupDMABuff_Wrapper(addr, size, fd);
}

hcclResult_t HCCL_API_CALL hcclReduceScatter_Original(const void*     sendbuff,
                                                      void*           recvbuff,
                                                      size_t          recvcount,
                                                      hcclDataType_t  datatype,
                                                      hcclRedOp_t     reduceOp,
                                                      hcclComm_t      comm,
                                                      synStreamHandle stream_handle)
{
    return hcclReduceScatter_Wrapper(sendbuff, recvbuff, recvcount, datatype, reduceOp, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclAllReduce_Original(const void*     sendbuff,
                                                  void*           recvbuff,
                                                  size_t          count,
                                                  hcclDataType_t  datatype,
                                                  hcclRedOp_t     reduceOp,
                                                  hcclComm_t      comm,
                                                  synStreamHandle stream_handle)
{
    return hcclAllReduce_Wrapper(sendbuff, recvbuff, count, datatype, reduceOp, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclReduce_Original(const void*     sendbuff,
                                               void*           recvbuff,
                                               size_t          count,
                                               hcclDataType_t  datatype,
                                               hcclRedOp_t     reduceOp,
                                               int             root,
                                               hcclComm_t      comm,
                                               synStreamHandle stream_handle)
{
    return hcclReduce_Wrapper(sendbuff, recvbuff, count, datatype, reduceOp, root, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclBcast_Original(void*           buff,
                                              size_t          count,
                                              hcclDataType_t  datatype,
                                              int             root,
                                              hcclComm_t      comm,
                                              synStreamHandle stream_handle)
{
    return hcclBroadcast(buff, buff, count, datatype, root, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclBroadcast_Original(const void*     sendbuff,
                                                  void*           recvbuff,
                                                  size_t          count,
                                                  hcclDataType_t  datatype,
                                                  int             root,
                                                  hcclComm_t      comm,
                                                  synStreamHandle stream_handle)
{
    return hcclBroadcast_Wrapper(sendbuff, recvbuff, count, datatype, root, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclAllGather_Original(const void*     sendbuff,
                                                  void*           recvbuff,
                                                  size_t          sendcount,
                                                  hcclDataType_t  datatype,
                                                  hcclComm_t      comm,
                                                  synStreamHandle stream_handle)
{
    return hcclAllGather_Wrapper(sendbuff, recvbuff, sendcount, datatype, comm, stream_handle);
}

hcclResult_t hcclAlltoAll_Original(const void*     sendbuff,
                                   void*           recvbuff,
                                   size_t          count,
                                   hcclDataType_t  datatype,
                                   hcclComm_t      comm,
                                   synStreamHandle stream_handle)
{
    return hcclAlltoAll_Wrapper(sendbuff, recvbuff, count, datatype, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclBarrier_Original([[maybe_unused]] hcclComm_t      comm_handle,
                                                [[maybe_unused]] synStreamHandle stream_handle)
{
    HCCL_TRY
    LOG_ERR(HCL_API, "Unsupported API for Gen2");
    HCCL_API_EXIT(hcclUnsupported)
}

hcclResult_t HCCL_API_CALL hcclSend_Original(const void*     sendbuff,
                                             size_t          count,
                                             hcclDataType_t  datatype,
                                             int             peer,
                                             hcclComm_t      comm,
                                             synStreamHandle stream_handle)
{
    return hcclSend_Wrapper(sendbuff, count, datatype, peer, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclRecv_Original(void*           recvbuff,
                                             size_t          count,
                                             hcclDataType_t  datatype,
                                             int             peer,
                                             hcclComm_t      comm,
                                             synStreamHandle stream_handle)
{
    return hcclRecv_Wrapper(recvbuff, count, datatype, peer, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclGroupStart_Original()
{
    return hcclGroupStart_Wrapper();
}

hcclResult_t HCCL_API_CALL hcclGroupEnd_Original()
{
    return hcclGroupEnd_Wrapper();
}

hcclResult_t hcclInitDevice_Original([[maybe_unused]] const synDeviceId deviceId)
{
    HCCL_TRY
    hcclResult_t status = hccl_ctx.init_device(hccl_ctx.generateApiId());
    HCCL_API_EXIT(status)
}

hcclResult_t hcclDestroyDevice_Original(const synDeviceId deviceId)
{
    return hcclDestroyDevice_Wrapper(deviceId);
}

hcclResult_t hcclEventRecord_Original(hcclEventHandle* eventHandle, synStreamHandle streamHandle)
{
    HCCL_TRY
    RETURN_ON_INVALID_STREAM(streamHandle);
    hcclResult_t status = hccl_device()->networkFlush((HCL_Request*)eventHandle, streamHandle);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclSynchronizeEvent_Original([[maybe_unused]] const hcclEventHandle& eventHandle,
                                           [[maybe_unused]] uint64_t               microSeconds)
{
    HCCL_TRY
    LOG_ERR(HCL_API, "Unsupported API for Gen2");
    HCCL_API_EXIT(hcclUnsupported)
}

hcclResult_t hcclFlushSubmissions_Original(synStreamHandle streamHandle)
{
    HCCL_TRY
    RETURN_ON_INVALID_STREAM(streamHandle);
    hccl_device()->waitForAllEvents(synStreamGetPhysicalQueueOffset(streamHandle), false);
    HCCL_API_EXIT(hcclSuccess)
}

hcclResult_t hcclFlushSubmissionsAllStreams_Original()
{
    HCCL_TRY
    hccl_device()->waitForAllEvents(false);
    HCCL_API_EXIT(hcclSuccess)
}

hcclResult_t hcclSynchronizeStream_Original(synStreamHandle streamHandle)
{
    HCCL_TRY
    RETURN_ON_INVALID_STREAM(streamHandle);
    hccl_device()->waitForAllEvents(synStreamGetPhysicalQueueOffset(streamHandle), true);
    HCCL_API_EXIT(hcclSuccess)
}

hcclResult_t hcclSynchronizeAllStreams_Original()
{
    HCCL_TRY
    hccl_device()->waitForAllEvents(true);
    HCCL_API_EXIT(hcclSuccess)
}

hcclResult_t hcclDFA_Original(DfaStatus& dfaStatus, void (*dfaLogFunc)(int, const char*))
{
    if (dfaStatus.hasError(DfaErrorCode::scalTdrFailed))
    {
        tdrDetectionFlag = true;
    }

    LOG_INFO(HCL, "DFA asked to dump info");
    dfaLogFunc(HLLOG_LEVEL_INFO, "HCL DFA not implemented");
    return hcclSuccess;
}

hcclResult_t hcclDfaUpdateState_Original(DfaPhase dfaPhase)
{
    bool updateErr = false;
    switch (dfaPhase)
    {
        case DfaPhase::NONE:
            if (g_dfaPhase == DfaPhase::STARTED)
            {
                g_dfaMutex.unlock();
            }
            else
            {
                updateErr = true;
            }
            g_status = hcclResult_t::hcclSuccess;
            break;

        case DfaPhase::STARTED:
        {
            if (g_dfaPhase != DfaPhase::STARTED)
            {
                g_dfaMutex.lock();
            }
            else
            {
                updateErr = true;
            }
            g_status = hcclResult_t::hcclInternalError;
            break;
        }

        case DfaPhase::ENDED:
            if (g_dfaPhase == DfaPhase::STARTED)
            {
                g_dfaMutex.unlock();
            }
            else
            {
                updateErr = true;
            }
            g_status = hcclResult_t::hcclInternalError;
            break;
    }

    if (updateErr)
    {
        LOG_ERR(HCL, "Unexpected Dfa phase update. Old {} new {}", g_dfaPhase, dfaPhase);
    }
    LOG_INFO(HCL_API, "Dfa state change. Old {} new {}", g_dfaPhase, dfaPhase);
    g_dfaPhase = dfaPhase;
    return hcclSuccess;
}

hcclResult_t hcclGetVersionString_Original(char* pVersion, const unsigned len)
{
    HCCL_TRY
    RETURN_ON_NULL_ARG(pVersion);

    getHclVersion(pVersion, len);
    LOG_DEBUG(HCL_API, "HCCL Version String is: {}", pVersion);
    HCCL_API_EXIT(hcclSuccess)
}

hcclResult_t HCCL_API_CALL hcclDeviceInit_Original(void* device, void* context)
{
    return hcclDeviceInit_Wrapper(device, context);
}

static struct hccl_functions_pointers default_functions_pointers_table = {
    .pfn_hcclGetVersion                 = hcclGetVersion_Original,
    .pfn_hcclGetUniqueId                = hcclGetUniqueId_Original,
    .pfn_hcclCommInitRank               = hcclCommInitRank_Original,
    .pfn_hcclCommInitAll                = hcclCommInitAll_Original,
    .pfn_hcclCommDestroy                = hcclCommDestroy_Original,
    .pfn_hcclCommAbort                  = hcclCommAbort_Original,
    .pfn_hcclGetErrorString             = hcclGetErrorString_Original,
    .pfn_hcclCommGetAsyncError          = hcclCommGetAsyncError_Original,
    .pfn_hcclCommCount                  = hcclCommCount_Original,
    .pfn_hcclCommSynDevice              = hcclCommSynDevice_Original,
    .pfn_hcclCommUserRank               = hcclCommUserRank_Original,
    .pfn_hcclLookupDMABuff              = hcclLookupDMABuff_Original,
    .pfn_hcclReduce                     = hcclReduce_Original,
    .pfn_hcclBcast                      = hcclBcast_Original,
    .pfn_hcclBroadcast                  = hcclBroadcast_Original,
    .pfn_hcclAllReduce                  = hcclAllReduce_Original,
    .pfn_hcclReduceScatter              = hcclReduceScatter_Original,
    .pfn_hcclAllGather                  = hcclAllGather_Original,
    .pfn_hcclAlltoAll                   = hcclAlltoAll_Original,
    .pfn_hcclBarrier                    = hcclBarrier_Original,
    .pfn_hcclSend                       = hcclSend_Original,
    .pfn_hcclRecv                       = hcclRecv_Original,
    .pfn_hcclGroupStart                 = hcclGroupStart_Original,
    .pfn_hcclGroupEnd                   = hcclGroupEnd_Original,
    .pfn_hcclInitDevice                 = hcclInitDevice_Original,
    .pfn_hcclDestroyDevice              = hcclDestroyDevice_Original,
    .pfn_hcclEventRecord                = hcclEventRecord_Original,
    .pfn_hcclSynchronizeEvent           = hcclSynchronizeEvent_Original,
    .pfn_hcclFlushSubmissions           = hcclFlushSubmissions_Original,
    .pfn_hcclFlushSubmissionsAllStreams = hcclFlushSubmissionsAllStreams_Original,
    .pfn_hcclSynchronizeStream          = hcclSynchronizeStream_Original,
    .pfn_hcclSynchronizeAllStreams      = hcclSynchronizeAllStreams_Original,
    .pfn_hcclDFA                        = hcclDFA_Original,
    .pfn_hcclDfaUpdateState             = hcclDfaUpdateState_Original,
    .pfn_hcclGetVersionString           = hcclGetVersionString_Original,
    .pfn_hcclCommFinalize               = hcclCommFinalize_Original,
    .pfn_hcclDeviceInit                 = hcclDeviceInit_Original};
// functions_pointers_table will maintain the current functions pointers table
// Initialized to the original functions
static struct hccl_functions_pointers* functions_pointers_table = &default_functions_pointers_table;

static void*                s_shimLibHandle       = nullptr;
static PFN_ShimGetFunctions s_pfnShimGetFunctions = nullptr;
static PFN_ShimFinish       s_pfnShimFinish       = nullptr;

static void HCCL_StartShim(void)
{
    if (s_shimLibHandle == nullptr)
    {
        const char* envValue = getenv("HABANA_SHIM_DISABLE");
        if (envValue == nullptr || strcmp(envValue, "1"))
        {
            LOG_DEBUG(HCL, "Engaging shim layer");
            s_shimLibHandle = dlopen(SHIM_LIB_NAME, RTLD_LAZY);
            if (s_shimLibHandle != nullptr)
            {
                void* fn = dlsym(s_shimLibHandle, SHIM_GET_FUNCTIONS);
                if (fn != nullptr)
                {
                    LOG_DEBUG(HCL, "shim layer initialized successfully");
                    s_pfnShimGetFunctions = reinterpret_cast<PFN_ShimGetFunctions>(fn);
                    s_pfnShimFinish       = reinterpret_cast<PFN_ShimFinish>(dlsym(s_shimLibHandle, SHIM_FINISH));
                    /*
                     * start/stop profiling is not supported at the moment.
                     * Currently, we call ShimGetFunctions only once in the initialization
                     * To support profiling during execution,
                     * we will have to call it before every (or specific) API call
                     */
                    functions_pointers_table = static_cast<hccl_functions_pointers*>(
                        s_pfnShimGetFunctions(SHIM_API_HCCL, functions_pointers_table));
                }
                else
                {
                    LOG_ERR(HCL, "shim layer entry point function was not found");
                    dlclose(s_shimLibHandle);
                    s_shimLibHandle = nullptr;
                }
            }
            else
            {
                LOG_ERR(HCL, "Could not load shim layer binary - {}", SHIM_LIB_NAME);
                LOG_ERR(HCL, "dlerror: {} ", dlerror());
            }
        }
    }
}

static void HCCL_StopShim(void)
{
    functions_pointers_table = &default_functions_pointers_table;
    if (s_shimLibHandle != nullptr)
    {
        if (s_pfnShimFinish != nullptr)
        {
            s_pfnShimFinish(SHIM_API_HCCL);
        }
        dlclose(s_shimLibHandle);
        s_shimLibHandle = nullptr;
    }
}

namespace HclGen2
{

hcclResult_t HCCL_API_CALL hcclGetVersion_impl(int* version)
{
    HCL_API_LOG_ENTRY("(&version={:p})", (void*)version);
    return (*functions_pointers_table->pfn_hcclGetVersion)(version);
}

hcclResult_t HCCL_API_CALL hcclGetUniqueId_impl(hcclUniqueId* uniqueId)
{
    HCL_API_LOG_ENTRY("(&uniqueId={:p})", (void*)uniqueId);
    return (*functions_pointers_table->pfn_hcclGetUniqueId)(uniqueId);
}

hcclResult_t HCCL_API_CALL hcclCommInitRank_impl(hcclComm_t* comm, int nranks, hcclUniqueId commId, int rank)
{
    HCCL_StartShim();
    return (*functions_pointers_table->pfn_hcclCommInitRank)(comm, nranks, commId, rank);
}

hcclResult_t HCCL_API_CALL hcclCommInitAll_impl(hcclComm_t* comm, int ndev, const int* devlist)
{
    HCL_API_LOG_ENTRY("(&comm={:p}, ndev={}, &devlist={})", (void*)comm, ndev, (void*)devlist);
    HCCL_StartShim();

    return (*functions_pointers_table->pfn_hcclCommInitAll)(comm, ndev, devlist);
}

hcclResult_t HCCL_API_CALL hcclCommFinalize_impl(hcclComm_t comm)
{
    HCL_API_LOG_ENTRY("(&comm={:p})", (void*)comm);
    return (*functions_pointers_table->pfn_hcclCommFinalize)(comm);
}

bool HCCL_API_CALL hcclIsACcbHalfFull_impl(const unsigned archStreamIdx)
{
    HCCL_TRY
    const bool res = hcclIsACcbHalfFull_Original(archStreamIdx);
    HCL_API_EXIT(res)
}

void HCCL_API_CALL hcclSetTraceMarker_impl(const synStreamHandle stream_handle, uint32_t val)
{
    hcclResult_t status = syncHCLStreamHandle(stream_handle);
    if (status != hcclSuccess) return;
    hcclSetTraceMarker_Original(stream_handle, val);
}

hcclResult_t HCCL_API_CALL hcclCommDestroy_impl(hcclComm_t comm)
{
    HCL_API_LOG_ENTRY("(&comm={:p})", (void*)comm);
    hcclResult_t status = (*functions_pointers_table->pfn_hcclCommDestroy)(comm);
    HCCL_StopShim();

    return status;
}

hcclResult_t HCCL_API_CALL hcclCommAbort_impl(hcclComm_t comm)
{
    HCL_API_LOG_ENTRY("(&comm={:p})", (void*)comm);
    return (*functions_pointers_table->pfn_hcclCommAbort)(comm);
}

HCCL_API_CALL const char* hcclGetErrorString_impl(hcclResult_t result)
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclGetErrorString)(result);
}

hcclResult_t HCCL_API_CALL hcclCommGetAsyncError_impl(hcclComm_t comm, hcclResult_t* asyncError)
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclCommGetAsyncError)(comm, asyncError);
}

hcclResult_t HCCL_API_CALL hcclCommCount_impl(hcclComm_t comm, int* count)
{
    HCL_API_LOG_ENTRY("(&comm={:p}, &count={:p})", (void*)comm, (void*)count);
    return (*functions_pointers_table->pfn_hcclCommCount)(comm, count);
}

hcclResult_t HCCL_API_CALL hcclCommSynDevice_impl(hcclComm_t comm, int* device)
{
    HCL_API_LOG_ENTRY("(&comm={:p}, &device={:p})", (void*)comm, (void*)device);
    return (*functions_pointers_table->pfn_hcclCommSynDevice)(comm, device);
}

hcclResult_t HCCL_API_CALL hcclCommUserRank_impl(hcclComm_t comm, int* rank)
{
    HCL_API_LOG_ENTRY("(&comm={:p}, &rank={:p})", (void*)comm, (void*)rank);
    return (*functions_pointers_table->pfn_hcclCommUserRank)(comm, rank);
}

int HCCL_API_CALL hcclLookupDMABuff_impl(uint64_t addr, uint64_t size, int* fd)
{
    HCL_API_LOG_ENTRY("(&addr={:p}, &size={:p})", (void*)addr, (void*)size);
    return (*functions_pointers_table->pfn_hcclLookupDMABuff)(addr, size, fd);
}

hcclResult_t HCCL_API_CALL hcclReduceScatter_impl(const void*     sendbuff,
                                                  void*           recvbuff,
                                                  size_t          recvcount,
                                                  hcclDataType_t  datatype,
                                                  hcclRedOp_t     reduceOp,
                                                  hcclComm_t      comm,
                                                  synStreamHandle stream_handle)
{
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hccl_comm);

    hccl_comm->incCollectiveCtr();

    HCL_API_LOG_ENTRY(
        "rank={}/{}, oam={}, (sendbuff={:p}, recvbuff={:p}, recvcount={}, datatype={}, reduceOp={}, uniqId={}, "
        "stream_handle={:p}) - collective#=0x{:x}",
        hccl_comm->user_rank(),
        hccl_comm->getCommSize(),
        hccl_device()->getHwModuleId(),
        (void*)sendbuff,
        (void*)recvbuff,
        recvcount,
        to_string(datatype),
        to_string(reduceOp),
        hccl_comm->getCommUniqueId(),
        (void*)stream_handle,
        hccl_comm->getCollectiveCtr());

    hcclResult_t status = syncHCLStreamHandle(stream_handle);
    if (status != hcclSuccess) return status;

    LOG_SYNC_DBG(ReduceScatter,
                 "#Lines: 1 {:#x} {:#x} {:#x} {:#x} {:#x}",
                 TO64(stream_handle),
                 TO64(hccl_comm),
                 TO64(sendbuff),
                 TO64(recvbuff),
                 recvcount * hccl_data_type_elem_size(datatype));

    return (*functions_pointers_table
                 ->pfn_hcclReduceScatter)(sendbuff, recvbuff, recvcount, datatype, reduceOp, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclAllReduce_impl(const void*     sendbuff,
                                              void*           recvbuff,
                                              size_t          count,
                                              hcclDataType_t  datatype,
                                              hcclRedOp_t     reduceOp,
                                              hcclComm_t      comm,
                                              synStreamHandle stream_handle)
{
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hccl_comm);

    hccl_comm->incCollectiveCtr();

    HCL_API_LOG_ENTRY("rank={}/{}, oam={}, (sendbuff={:p}, recvbuff={:p}, count={}, datatype={}, reduceOp={}, "
                      "uniqId={}, stream_handle={:p}) - "
                      "collective#=0x{:x}",
                      hccl_comm->user_rank(),
                      hccl_comm->getCommSize(),
                      hccl_device()->getHwModuleId(),
                      (void*)sendbuff,
                      (void*)recvbuff,
                      count,
                      to_string(datatype),
                      to_string(reduceOp),
                      hccl_comm->getCommUniqueId(),
                      (void*)stream_handle,
                      hccl_comm->getCollectiveCtr());

    hcclResult_t status = syncHCLStreamHandle(stream_handle);
    if (status != hcclSuccess) return status;

    LOG_SYNC_DBG(AllReduce,
                 "#Lines: 1 {:#x} {:#x} {:#x} {:#x} {:#x}",
                 TO64(stream_handle),
                 TO64(hccl_comm),
                 TO64(sendbuff),
                 TO64(recvbuff),
                 count * hccl_data_type_elem_size(datatype));

    return (*functions_pointers_table
                 ->pfn_hcclAllReduce)(sendbuff, recvbuff, count, datatype, reduceOp, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclReduce_impl(const void*     sendbuff,
                                           void*           recvbuff,
                                           size_t          count,
                                           hcclDataType_t  datatype,
                                           hcclRedOp_t     reduceOp,
                                           int             root,
                                           hcclComm_t      comm,
                                           synStreamHandle stream_handle)
{
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hccl_comm);

    hccl_comm->incCollectiveCtr();

    HCL_API_LOG_ENTRY("rank={}/{}, oam={}, (sendbuff={:p}, recvbuff={:p}, count={}, datatype={}, reduceOp={}, "
                      "root={}, uniqId={}, stream_handle={:p}) - collective#=0x{:x}",
                      hccl_comm->user_rank(),
                      hccl_comm->getCommSize(),
                      hccl_device()->getHwModuleId(),
                      (void*)sendbuff,
                      (void*)recvbuff,
                      count,
                      to_string(datatype),
                      to_string(reduceOp),
                      root,
                      hccl_comm->getCommUniqueId(),
                      (void*)stream_handle,
                      hccl_comm->getCollectiveCtr());

    hcclResult_t status = syncHCLStreamHandle(stream_handle);
    if (status != hcclSuccess) return status;

    LOG_SYNC_DBG(Reduce,
                 "#Lines: 1 {:#x} {:#x} {:#x} {:#x} {:#x}",
                 TO64(stream_handle),
                 TO64(hccl_comm),
                 TO64(sendbuff),
                 TO64(recvbuff),
                 count * hccl_data_type_elem_size(datatype));

    return (*functions_pointers_table
                 ->pfn_hcclReduce)(sendbuff, recvbuff, count, datatype, reduceOp, root, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclBcast_impl(void*           buff,
                                          size_t          count,
                                          hcclDataType_t  datatype,
                                          int             root,
                                          hcclComm_t      comm,
                                          synStreamHandle stream_handle)
{
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hccl_comm);

    hccl_comm->incCollectiveCtr();

    HCL_API_LOG_ENTRY("rank={}/{}, oam={}, (buff={:p}, count={}, datatype={}, root={}, uniqId={}, "
                      "stream_handle={:p}) - collective#=0x{:x}",
                      hccl_comm->user_rank(),
                      hccl_comm->getCommSize(),
                      hccl_device()->getHwModuleId(),
                      (void*)buff,
                      count,
                      to_string(datatype),
                      root,
                      hccl_comm->getCommUniqueId(),
                      (void*)stream_handle,
                      hccl_comm->getCollectiveCtr());

    hcclResult_t status = syncHCLStreamHandle(stream_handle);
    if (status != hcclSuccess) return status;

    return (*functions_pointers_table->pfn_hcclBcast)(buff, count, datatype, root, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclBroadcast_impl(const void*     sendbuff,
                                              void*           recvbuff,
                                              size_t          count,
                                              hcclDataType_t  datatype,
                                              int             root,
                                              hcclComm_t      comm_handle,
                                              synStreamHandle stream_handle)
{
    auto* hccl_comm = hccl_ctx.communicator(comm_handle);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hccl_comm);

    hccl_comm->incCollectiveCtr();

    HCL_API_LOG_ENTRY("rank={}/{}, oam={}, (sendbuff={:p}, recvbuff={:p}, count={}, datatype={}, root={}, "
                      "uniqId={}, stream_handle={:p}) - collective#=0x{:x}",
                      hccl_comm->user_rank(),
                      hccl_comm->getCommSize(),
                      hccl_device()->getHwModuleId(),
                      (void*)sendbuff,
                      (void*)recvbuff,
                      count,
                      to_string(datatype),
                      root,
                      hccl_comm->getCommUniqueId(),
                      (void*)stream_handle,
                      hccl_comm->getCollectiveCtr());

    hcclResult_t status = syncHCLStreamHandle(stream_handle);
    if (status != hcclSuccess) return status;

    LOG_SYNC_DBG(Broadcast,
                 "#Lines: 1 {:#x} {:#x} {:#x} {:#x} {:#x}",
                 TO64(stream_handle),
                 TO64(hccl_comm),
                 TO64(sendbuff),
                 TO64(recvbuff),
                 count * hccl_data_type_elem_size(datatype));

    return (*functions_pointers_table
                 ->pfn_hcclBroadcast)(sendbuff, recvbuff, count, datatype, root, comm_handle, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclAllGather_impl(const void*     sendbuff,
                                              void*           recvbuff,
                                              size_t          sendcount,
                                              hcclDataType_t  datatype,
                                              hcclComm_t      comm_handle,
                                              synStreamHandle stream_handle)
{
    auto* hccl_comm = hccl_ctx.communicator(comm_handle);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hccl_comm);

    hccl_comm->incCollectiveCtr();

    HCL_API_LOG_ENTRY("rank={}/{}, oam={}, (sendbuff={:p}, recvbuff={:p}, sendcount={}, datatype={}, uniqId={}, "
                      "stream_handle={:p}) - collective#=0x{:x}",
                      hccl_comm->user_rank(),
                      hccl_comm->getCommSize(),
                      hccl_device()->getHwModuleId(),
                      (void*)sendbuff,
                      (void*)recvbuff,
                      sendcount,
                      to_string(datatype),
                      hccl_comm->getCommUniqueId(),
                      (void*)stream_handle,
                      hccl_comm->getCollectiveCtr());

    hcclResult_t status = syncHCLStreamHandle(stream_handle);
    if (status != hcclSuccess) return status;

    LOG_SYNC_DBG(AllGather,
                 "#Lines: 1 {:#x} {:#x} {:#x} {:#x} {:#x}",
                 TO64(stream_handle),
                 TO64(hccl_comm),
                 TO64(sendbuff),
                 TO64(recvbuff),
                 sendcount * hccl_data_type_elem_size(datatype));

    return (*functions_pointers_table
                 ->pfn_hcclAllGather)(sendbuff, recvbuff, sendcount, datatype, comm_handle, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclBarrier_impl([[maybe_unused]] hcclComm_t      comm_handle,
                                            [[maybe_unused]] synStreamHandle stream_handle)
{
    HCCL_TRY
    LOG_ERR(HCL_API, "Unsupported API for Gen2");
    HCCL_API_EXIT(hcclUnsupported)
}

hcclResult_t HCCL_API_CALL hcclAlltoAll_impl(const void*     sendbuff,
                                             void*           recvbuff,
                                             size_t          count,
                                             hcclDataType_t  datatype,
                                             hcclComm_t      comm,
                                             synStreamHandle stream_handle)
{
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hccl_comm);

    hccl_comm->incCollectiveCtr();

    HCL_API_LOG_ENTRY("rank={}/{}, oam={}, (sendbuff={:p}, recvbuff={:p}, count={}, datatype={}, uniqId={}, "
                      "stream_handle={:p}) - collective#=0x{:x}",
                      hccl_comm->user_rank(),
                      hccl_comm->getCommSize(),
                      hccl_device()->getHwModuleId(),
                      (void*)sendbuff,
                      (void*)recvbuff,
                      count,
                      to_string(datatype),
                      hccl_comm->getCommUniqueId(),
                      (void*)stream_handle,
                      hccl_comm->getCollectiveCtr());

    hcclResult_t status = syncHCLStreamHandle(stream_handle);
    if (status != hcclSuccess) return status;

    LOG_SYNC_DBG(AllToAll,
                 "#Lines: 1 {:#x} {:#x} {:#x} {:#x} {:#x}",
                 TO64(stream_handle),
                 TO64(hccl_comm),
                 TO64(sendbuff),
                 TO64(recvbuff),
                 count * hccl_data_type_elem_size(datatype));

    return (*functions_pointers_table->pfn_hcclAlltoAll)(sendbuff, recvbuff, count, datatype, comm, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclSend_impl(const void*     sendbuff,
                                         size_t          count,
                                         hcclDataType_t  datatype,
                                         int             peer,
                                         hcclComm_t      comm_handle,
                                         synStreamHandle stream_handle)
{
    auto* hccl_comm = hccl_ctx.communicator(comm_handle);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);

    const uint64_t send_cntr = hccl_comm->incSendCtr((HCL_Rank)peer);
    HCL_API_LOG_ENTRY(
        "rank={}/{}, oam={}, (sendbuff={:p}, count={}, datatype={}, uniqId={}, stream_handle={:p}, peer={}, send#={})",
        hccl_comm->user_rank(),
        hccl_comm->getCommSize(),
        hccl_device()->getHwModuleId(),
        (void*)sendbuff,
        count,
        to_string(datatype),
        hccl_comm->getCommUniqueId(),
        (void*)stream_handle,
        peer,
        send_cntr);

    hcclResult_t status = syncHCLStreamHandle(stream_handle);
    if (status != hcclSuccess) return status;

    LOG_SYNC_DBG(Send,
                 "#Lines: 1 {:#x} {:#x} {:#x} {:#x}",
                 TO64(stream_handle),
                 TO64(hccl_comm),
                 TO64(sendbuff),
                 count * hccl_data_type_elem_size(datatype));

    return (*functions_pointers_table->pfn_hcclSend)(sendbuff, count, datatype, peer, comm_handle, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclRecv_impl(void*           recvbuff,
                                         size_t          count,
                                         hcclDataType_t  datatype,
                                         int             peer,
                                         hcclComm_t      comm_handle,
                                         synStreamHandle stream_handle)
{
    auto* hccl_comm = hccl_ctx.communicator(comm_handle);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);

    const uint64_t recv_cntr = hccl_comm->incRecvCtr((HCL_Rank)peer);
    HCL_API_LOG_ENTRY(
        "rank={}/{}, oam={}, (recvbuff={:p}, count={}, datatype={}, uniqId={}, stream_handle={:p}, peer={}, recv#={})",
        hccl_comm->user_rank(),
        hccl_comm->getCommSize(),
        hccl_device()->getHwModuleId(),
        recvbuff,
        count,
        to_string(datatype),
        hccl_comm->getCommUniqueId(),
        (void*)stream_handle,
        peer,
        recv_cntr);

    hcclResult_t status = syncHCLStreamHandle(stream_handle);
    if (status != hcclSuccess) return status;

    LOG_SYNC_DBG(Receive,
                 "#Lines: 1 {:#x} {:#x} {:#x} {:#x}",
                 TO64(stream_handle),
                 TO64(hccl_comm),
                 TO64(recvbuff),
                 count * hccl_data_type_elem_size(datatype));

    return (*functions_pointers_table->pfn_hcclRecv)(recvbuff, count, datatype, peer, comm_handle, stream_handle);
}

hcclResult_t HCCL_API_CALL hcclGroupStart_impl()
{
    HCL_API_LOG_ENTRY();

    LOG_SYNC_DBG(GroupStart, "#Lines: 1");

    return (*functions_pointers_table->pfn_hcclGroupStart)();
}

hcclResult_t HCCL_API_CALL hcclGroupEnd_impl()
{
    HCL_API_LOG_ENTRY();

    LOG_SYNC_DBG(GroupEnd, "#Lines: 1");

    return (*functions_pointers_table->pfn_hcclGroupEnd)();
}

hcclResult_t HCCL_API_CALL hcclInitDevice(const synDeviceId deviceId)
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclInitDevice)(deviceId);
}

hcclResult_t HCCL_API_CALL hcclDestroyDevice(const synDeviceId deviceid)
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclDestroyDevice)(deviceid);
}

hcclResult_t HCCL_API_CALL hcclEventRecord(hcclEventHandle* eventHandle, synStreamHandle streamHandle)
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclEventRecord)(eventHandle, streamHandle);
}

hcclResult_t HCCL_API_CALL hcclSynchronizeEvent(const hcclEventHandle& eventHandle, uint64_t microSeconds)
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclSynchronizeEvent)(eventHandle, microSeconds);
}

hcclResult_t HCCL_API_CALL hcclFlushSubmissions(synStreamHandle streamHandle)
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclFlushSubmissions)(streamHandle);
}

hcclResult_t HCCL_API_CALL hcclFlushSubmissionsAllStreams()
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclFlushSubmissionsAllStreams)();
}

hcclResult_t HCCL_API_CALL hcclSynchronizeStream(synStreamHandle streamHandle)
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclSynchronizeStream)(streamHandle);
}

hcclResult_t HCCL_API_CALL hcclSynchronizeAllStreams()
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclSynchronizeAllStreams)();
}

hcclResult_t HCCL_API_CALL hcclDFA(DfaStatus& dfaStatus, void (*dfaLogFunc)(int, const char*))
{
    HCL_API_LOG_ENTRY();
    return (*functions_pointers_table->pfn_hcclDFA)(dfaStatus, dfaLogFunc);
}

hcclResult_t HCCL_API_CALL hcclDfaUpdateState(DfaPhase dfaPhase)
{
    HCL_API_LOG_ENTRY("update Dfa phase to {}", dfaPhase);
    return (*functions_pointers_table->pfn_hcclDfaUpdateState)(dfaPhase);
}

hcclResult_t HCCL_API_CALL hcclGetVersionString(char* pVersion, const unsigned len)
{
    HCL_API_LOG_ENTRY("pVersion={:#x}, len={}", TO64(pVersion), len);
    return (*functions_pointers_table->pfn_hcclGetVersionString)(pVersion, len);
}

hcclResult_t HCCL_API_CALL hcclDeviceInit_impl(void* device, void* context)
{
    HCL_API_LOG_ENTRY("(&device={:p}, &context={:p})", device, context);
    return (*functions_pointers_table->pfn_hcclDeviceInit)(device, context);
}

}  // namespace HclGen2
