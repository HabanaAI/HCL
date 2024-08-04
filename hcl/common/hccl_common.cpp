/******************************************************************************
 * Copyright (C) 2020-2024 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

// This file should include only header files from $HCL_ROOT/include/** and hccl_gen2_impl.h/hccl_gaudi_impl.h

#include "hccl_types.h"                                        // for hcclResult_t, hcclC...
#include "internal/hccl_internal.h"  // for hcclEventHandle
#include "../src/hccl/hccl_gen2_impl.h"                        // for Gen2 hccl impl

#define HCCL_API_CALL __attribute__((visibility("default")))

static bool s_isGaudi = false;

hcclResult_t HCCL_API_CALL hcclGetVersion_impl(int* version)
{
    
        return (HclGen2::hcclGetVersion_impl(version));
    
}

hcclResult_t HCCL_API_CALL hcclGetUniqueId_impl(hcclUniqueId* uniqueId)
{
    
        return (HclGen2::hcclGetUniqueId_impl(uniqueId));
    
}

hcclResult_t HCCL_API_CALL hcclCommInitRank_impl(hcclComm_t* comm, int nranks, hcclUniqueId commId, int rank)
{
    
        return (HclGen2::hcclCommInitRank_impl(comm, nranks, commId, rank));
    
}

hcclResult_t HCCL_API_CALL hcclCommInitAll_impl(hcclComm_t* comm, int ndev, const int* devlist)
{
    
        return (HclGen2::hcclCommInitAll_impl(comm, ndev, devlist));
    
}

hcclResult_t HCCL_API_CALL hcclCommFinalize_impl(hcclComm_t comm)
{
    
        return (HclGen2::hcclCommFinalize_impl(comm));
    
}

bool HCCL_API_CALL hcclIsACcbHalfFull_impl(const unsigned archStreamIdx)
{
    
        return (HclGen2::hcclIsACcbHalfFull_impl(archStreamIdx));
    
}

hcclResult_t HCCL_API_CALL hcclCommDestroy_impl(hcclComm_t comm)
{
    
        return (HclGen2::hcclCommDestroy_impl(comm));
    
}

hcclResult_t HCCL_API_CALL hcclCommAbort_impl(hcclComm_t comm)
{
    
        return (HclGen2::hcclCommAbort_impl(comm));
    
}

HCCL_API_CALL const char* hcclGetErrorString_impl(hcclResult_t result)
{
    
        return (HclGen2::hcclGetErrorString_impl(result));
    
}

hcclResult_t HCCL_API_CALL hcclCommGetAsyncError_impl(hcclComm_t comm, hcclResult_t* asyncError)
{
    
        return (HclGen2::hcclCommGetAsyncError_impl(comm, asyncError));
    
}

hcclResult_t HCCL_API_CALL hcclCommCount_impl(hcclComm_t comm, int* count)
{
    
        return (HclGen2::hcclCommCount_impl(comm, count));
    
}

hcclResult_t HCCL_API_CALL hcclCommSynDevice_impl(hcclComm_t comm, int* device)
{
    
        return (HclGen2::hcclCommSynDevice_impl(comm, device));
    
}

hcclResult_t HCCL_API_CALL hcclCommUserRank_impl(hcclComm_t comm, int* rank)
{
    
        return (HclGen2::hcclCommUserRank_impl(comm, rank));
    
}

int HCCL_API_CALL hcclLookupDMABuff_impl(uint64_t addr, uint64_t size, int* fd)
{
    
        return (HclGen2::hcclLookupDMABuff_impl(addr, size, fd));
    
}

hcclResult_t HCCL_API_CALL hcclReduceScatter_impl(const void*     sendbuff,
                                                  void*           recvbuff,
                                                  size_t          recvcount,
                                                  hcclDataType_t  datatype,
                                                  hcclRedOp_t     reduceOp,
                                                  hcclComm_t      comm,
                                                  synStreamHandle stream_handle)
{
    
        return (
            HclGen2::hcclReduceScatter_impl(sendbuff, recvbuff, recvcount, datatype, reduceOp, comm, stream_handle));
    
}

hcclResult_t HCCL_API_CALL hcclAllReduce_impl(const void*     sendbuff,
                                              void*           recvbuff,
                                              size_t          count,
                                              hcclDataType_t  datatype,
                                              hcclRedOp_t     reduceOp,
                                              hcclComm_t      comm,
                                              synStreamHandle stream_handle)
{
    
        return (HclGen2::hcclAllReduce_impl(sendbuff, recvbuff, count, datatype, reduceOp, comm, stream_handle));
    
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
    
        return (HclGen2::hcclReduce_impl(sendbuff, recvbuff, count, datatype, reduceOp, root, comm, stream_handle));
    
}

hcclResult_t HCCL_API_CALL hcclBcast_impl(void*           buff,
                                          size_t          count,
                                          hcclDataType_t  datatype,
                                          int             root,
                                          hcclComm_t      comm,
                                          synStreamHandle stream_handle)
{
    
        return (HclGen2::hcclBcast_impl(buff, count, datatype, root, comm, stream_handle));
    
}

hcclResult_t HCCL_API_CALL hcclBroadcast_impl(const void*     sendbuff,
                                              void*           recvbuff,
                                              size_t          count,
                                              hcclDataType_t  datatype,
                                              int             root,
                                              hcclComm_t      comm_handle,
                                              synStreamHandle stream_handle)
{
    
        return (HclGen2::hcclBroadcast_impl(sendbuff, recvbuff, count, datatype, root, comm_handle, stream_handle));
    
}

hcclResult_t HCCL_API_CALL hcclAllGather_impl(const void*     sendbuff,
                                              void*           recvbuff,
                                              size_t          sendcount,
                                              hcclDataType_t  datatype,
                                              hcclComm_t      comm_handle,
                                              synStreamHandle stream_handle)
{
    
        return (HclGen2::hcclAllGather_impl(sendbuff, recvbuff, sendcount, datatype, comm_handle, stream_handle));
    
}

hcclResult_t HCCL_API_CALL hcclAlltoAll_impl(const void*     sendbuff,
                                             void*           recvbuff,
                                             size_t          count,
                                             hcclDataType_t  datatype,
                                             hcclComm_t      comm,
                                             synStreamHandle stream_handle)
{
    
        return (HclGen2::hcclAlltoAll_impl(sendbuff, recvbuff, count, datatype, comm, stream_handle));
    
}

hcclResult_t HCCL_API_CALL hcclBarrier_impl(hcclComm_t comm_handle, synStreamHandle stream_handle)
{
    
        return (HclGen2::hcclBarrier_impl(comm_handle, stream_handle));
    
}

hcclResult_t HCCL_API_CALL hcclSend_impl(const void*     sendbuff,
                                         size_t          count,
                                         hcclDataType_t  datatype,
                                         int             peer,
                                         hcclComm_t      comm_handle,
                                         synStreamHandle stream_handle)
{
    
        return (HclGen2::hcclSend_impl(sendbuff, count, datatype, peer, comm_handle, stream_handle));
    
}

hcclResult_t HCCL_API_CALL hcclRecv_impl(void*           recvbuff,
                                         size_t          count,
                                         hcclDataType_t  datatype,
                                         int             peer,
                                         hcclComm_t      comm_handle,
                                         synStreamHandle stream_handle)
{
    
        return (HclGen2::hcclRecv_impl(recvbuff, count, datatype, peer, comm_handle, stream_handle));
    
}

hcclResult_t HCCL_API_CALL hcclGroupStart_impl()
{
    
        return (HclGen2::hcclGroupStart_impl());
    
}

hcclResult_t HCCL_API_CALL hcclGroupEnd_impl()
{
    
        return (HclGen2::hcclGroupEnd_impl());
    
}

hcclResult_t HCCL_API_CALL hcclInitDevice(const synDeviceId deviceId)
{
    s_isGaudi = false;
    return (HclGen2::hcclInitDevice(deviceId));
}

hcclResult_t HCCL_API_CALL hcclInitDeviceGaudi(const synDeviceId deviceId)
{
    // Enable G1 calls into legacy_gaudi
    s_isGaudi = true;
    return (HclGen2::hcclInitDevice(deviceId));
}

hcclResult_t HCCL_API_CALL hcclDestroyDevice(const synDeviceId deviceid)
{
    
        return (HclGen2::hcclDestroyDevice(deviceid));
    
}

hcclResult_t HCCL_API_CALL hcclEventRecord(hcclEventHandle* eventHandle, synStreamHandle streamHandle)
{
    
        return (HclGen2::hcclEventRecord(eventHandle, streamHandle));
    
}

hcclResult_t HCCL_API_CALL hcclSynchronizeEvent(const hcclEventHandle& eventHandle, uint64_t microSeconds)
{
    
        return (HclGen2::hcclSynchronizeEvent(eventHandle, microSeconds));
    
}

hcclResult_t HCCL_API_CALL hcclFlushSubmissions(synStreamHandle streamHandle)
{
    
        return (HclGen2::hcclFlushSubmissions(streamHandle));
    
}

hcclResult_t HCCL_API_CALL hcclFlushSubmissionsAllStreams()
{
    
        return (HclGen2::hcclFlushSubmissionsAllStreams());
    
}

hcclResult_t HCCL_API_CALL hcclSynchronizeStream(synStreamHandle streamHandle)
{
    
        return (HclGen2::hcclSynchronizeStream(streamHandle));
    
}

hcclResult_t HCCL_API_CALL hcclSynchronizeAllStreams()
{
    
        return (HclGen2::hcclSynchronizeAllStreams());
    
}

hcclResult_t HCCL_API_CALL hcclDFA(DfaStatus& dfaStatus, void (*dfaLogFunc)(int, const char*))
{
    
        return (HclGen2::hcclDFA(dfaStatus, dfaLogFunc));
    
}

hcclResult_t HCCL_API_CALL hcclDfaUpdateState(DfaPhase dfaPhase)
{
    
        return (HclGen2::hcclDfaUpdateState(dfaPhase));
    
}

hcclResult_t HCCL_API_CALL hcclGetVersionString(char* pVersion, const unsigned len)
{
    
        return (HclGen2::hcclGetVersionString(pVersion, len));
    
}