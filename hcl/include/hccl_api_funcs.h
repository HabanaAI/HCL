/***************************************************************************
 * Copyright (C) 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 * Proprietary and confidential.
 ****************************************************************************
 */

#pragma once

#include "hccl_types.h"
#include "internal/hccl_internal.h"

struct hccl_functions_pointers
{
    hcclResult_t (*pfn_hcclGetVersion)(int* version);
    hcclResult_t (*pfn_hcclGetUniqueId)(hcclUniqueId* uniqueId);
    hcclResult_t (*pfn_hcclCommInitRank)(hcclComm_t* comm, int nranks, hcclUniqueId& commId, int rank);
    hcclResult_t (*pfn_hcclCommInitAll)(hcclComm_t* comm, int ndev, const int* devlist);
    hcclResult_t (*pfn_hcclCommDestroy)(hcclComm_t comm);
    hcclResult_t (*pfn_hcclCommAbort)(hcclComm_t comm);
    const char* (*pfn_hcclGetErrorString)(hcclResult_t result);
    hcclResult_t (*pfn_hcclCommGetAsyncError)(hcclComm_t comm, hcclResult_t* asyncError);
    hcclResult_t (*pfn_hcclCommCount)(hcclComm_t comm, int* count);
    hcclResult_t (*pfn_hcclCommSynDevice)(hcclComm_t comm, int* device);
    hcclResult_t (*pfn_hcclCommUserRank)(hcclComm_t comm, int* rank);
    int (*pfn_hcclLookupDMABuff)(uint64_t addr, uint64_t size, int* fd);
    hcclResult_t (*pfn_hcclReduce)(const void*     sendbuff,
                                   void*           recvbuff,
                                   size_t          count,
                                   hcclDataType_t  datatype,
                                   hcclRedOp_t     reduceOp,
                                   int             root,
                                   hcclComm_t      comm,
                                   synStreamHandle stream_handle);
    hcclResult_t (*pfn_hcclBcast)(void*           buff,
                                  size_t          count,
                                  hcclDataType_t  datatype,
                                  int             root,
                                  hcclComm_t      comm,
                                  synStreamHandle stream_handle);
    hcclResult_t (*pfn_hcclBroadcast)(const void*     sendbuff,
                                      void*           recvbuff,
                                      size_t          count,
                                      hcclDataType_t  datatype,
                                      int             root,
                                      hcclComm_t      comm,
                                      synStreamHandle stream_handle);
    hcclResult_t (*pfn_hcclAllReduce)(const void*     sendbuff,
                                      void*           recvbuff,
                                      size_t          count,
                                      hcclDataType_t  datatype,
                                      hcclRedOp_t     reduceOp,
                                      hcclComm_t      comm,
                                      synStreamHandle stream_handle);
    hcclResult_t (*pfn_hcclReduceScatter)(const void*     sendbuff,
                                          void*           recvbuff,
                                          size_t          recvcount,
                                          hcclDataType_t  datatype,
                                          hcclRedOp_t     reduceOp,
                                          hcclComm_t      comm,
                                          synStreamHandle stream_handle);
    hcclResult_t (*pfn_hcclAllGather)(const void*     sendbuff,
                                      void*           recvbuff,
                                      size_t          sendcount,
                                      hcclDataType_t  datatype,
                                      hcclComm_t      comm,
                                      synStreamHandle stream_handle);
    hcclResult_t (*pfn_hcclAlltoAll)(const void*     sendbuff,
                                     void*           recvbuff,
                                     size_t          sendcount,
                                     hcclDataType_t  datatype,
                                     hcclComm_t      comm_handle,
                                     synStreamHandle stream_handle);
    hcclResult_t (*pfn_hcclBarrier)(hcclComm_t comm, synStreamHandle stream_handle);
    hcclResult_t (*pfn_hcclSend)(const void*     sendbuff,
                                 size_t          count,
                                 hcclDataType_t  datatype,
                                 int             peer,
                                 hcclComm_t      comm,
                                 synStreamHandle stream);
    hcclResult_t (*pfn_hcclRecv)(void*           recvbuff,
                                 size_t          count,
                                 hcclDataType_t  datatype,
                                 int             peer,
                                 hcclComm_t      comm,
                                 synStreamHandle stream);
    hcclResult_t (*pfn_hcclGroupStart)();
    hcclResult_t (*pfn_hcclGroupEnd)();
    hcclResult_t (*pfn_hcclInitDevice)(const synDeviceId deviceId);
    hcclResult_t (*pfn_hcclDestroyDevice)(const synDeviceId deviceId);
    hcclResult_t (*pfn_hcclEventRecord)(hcclEventHandle* eventHandle, synStreamHandle streamHandle);
    hcclResult_t (*pfn_hcclSynchronizeEvent)(const hcclEventHandle& eventHandle, uint64_t microSeconds);
    hcclResult_t (*pfn_hcclFlushSubmissions)(synStreamHandle streamHandle);
    hcclResult_t (*pfn_hcclFlushSubmissionsAllStreams)();
    hcclResult_t (*pfn_hcclSynchronizeStream)(synStreamHandle streamHandle);
    hcclResult_t (*pfn_hcclSynchronizeAllStreams)();
    hcclResult_t (*pfn_hcclDFA)(DfaStatus& dfaStatus, void (*dfaLogFunc)(int, const char*));
    hcclResult_t (*pfn_hcclDfaUpdateState)(DfaPhase dfaPhase);
    hcclResult_t (*pfn_hcclCommFinalize)(hcclComm_t comm);
};
