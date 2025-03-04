#pragma once

#include "hccl_types.h"

hcclResult_t hcclGetVersion_Wrapper(int* version);

hcclResult_t hcclDeviceInit_Wrapper(void* device, void* context);

hcclResult_t hcclGetUniqueId_Wrapper(hcclUniqueId* uniqueId);

hcclResult_t hcclCommInitRank_Wrapper(hcclComm_t* comm, int nranks, hcclUniqueId& commId, int rank);

hcclResult_t hcclCommInitAll_Wrapper(hcclComm_t* comm, int ndev, const int* devlist);

hcclResult_t hcclCommFinalize_Wrapper(hcclComm_t comm);

hcclResult_t hcclCommDestroy_Wrapper(hcclComm_t comm);

hcclResult_t hcclCommAbort_Wrapper(hcclComm_t comm);

const char* hcclGetErrorString_Wrapper(hcclResult_t result);

hcclResult_t hcclCommGetAsyncError_Wrapper(hcclComm_t comm, hcclResult_t* asyncError);

hcclResult_t hcclCommCount_Wrapper(hcclComm_t comm, int* count);

hcclResult_t hcclCommSynDevice_Wrapper(hcclComm_t comm, int* device);

hcclResult_t hcclCommUserRank_Wrapper(hcclComm_t comm, int* rank);

int hcclLookupDMABuff_Wrapper(uint64_t addr, uint64_t size, int* fd);

hcclResult_t hcclReduceScatter_Wrapper(const void*    sendbuff,
                                       void*          recvbuff,
                                       size_t         recvcount,
                                       hcclDataType_t datatype,
                                       hcclRedOp_t    reduceOp,
                                       hcclComm_t     comm,
                                       void*          stream_handle);

hcclResult_t hcclReduce_Wrapper(const void*    sendbuff,
                                void*          recvbuff,
                                size_t         count,
                                hcclDataType_t datatype,
                                hcclRedOp_t    reduceOp,
                                int            root,
                                hcclComm_t     comm,
                                void*          stream_handle);

hcclResult_t hcclAllReduce_Wrapper(const void*    sendbuff,
                                   void*          recvbuff,
                                   size_t         count,
                                   hcclDataType_t datatype,
                                   hcclRedOp_t    reduceOp,
                                   hcclComm_t     comm,
                                   void*          stream_handle);

hcclResult_t hcclBroadcast_Wrapper(const void*    sendbuff,
                                   void*          recvbuff,
                                   size_t         count,
                                   hcclDataType_t datatype,
                                   int            root,
                                   hcclComm_t     comm,
                                   void*          stream_handle);

hcclResult_t hcclAllGather_Wrapper(const void*    sendbuff,
                                   void*          recvbuff,
                                   size_t         sendcount,
                                   hcclDataType_t datatype,
                                   hcclComm_t     comm,
                                   void*          stream_handle);

hcclResult_t hcclAlltoAll_Wrapper(const void*    sendbuff,
                                  void*          recvbuff,
                                  size_t         count,
                                  hcclDataType_t datatype,
                                  hcclComm_t     comm,
                                  void*          stream_handle);

hcclResult_t hcclSend_Wrapper(const void*    sendbuff,
                              size_t         count,
                              hcclDataType_t datatype,
                              int            peer,
                              hcclComm_t     comm,
                              void*          stream_handle);

hcclResult_t
hcclRecv_Wrapper(void* recvbuff, size_t count, hcclDataType_t datatype, int peer, hcclComm_t comm, void* stream_handle);

hcclResult_t hcclGroupStart_Wrapper();

hcclResult_t hcclGroupEnd_Wrapper();

hcclResult_t hcclInitDevice_Wrapper(const uint32_t deviceId);

hcclResult_t hcclDestroyDevice_Wrapper(const uint32_t deviceId);