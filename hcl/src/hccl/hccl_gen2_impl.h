/******************************************************************************
 * Copyright (C) 2024 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

//
// This file is the hccl functions implementation of Gen2, in libhcl.so, they are called from libhcl.so only

#pragma once

#include <cstddef>              // for size_t
#include "hccl_types.h"         // for hcclResult_t, hcclComm_t, hcclDataType_t
#include "synapse_api_types.h"  // for synStreamHandle

namespace HclGen2
{

hcclResult_t hcclInitDevice(const synDeviceId deviceId);
hcclResult_t hcclDestroyDevice(const synDeviceId deviceId);
hcclResult_t hcclEventRecord(hcclEventHandle* eventHandle, synStreamHandle streamHandle);
hcclResult_t hcclSynchronizeEvent(const hcclEventHandle& eventHandle, uint64_t microSeconds);
hcclResult_t hcclFlushSubmissions(synStreamHandle streamHandle);
hcclResult_t hcclFlushSubmissionsAllStreams();
hcclResult_t hcclSynchronizeStream(synStreamHandle streamHandle);
hcclResult_t hcclSynchronizeAllStreams();
hcclResult_t hcclDFA(DfaStatus& dfaStatus, void (*dfaLogFunc)(int, const char*));
hcclResult_t hcclDfaUpdateState(DfaPhase dfaPhase);
hcclResult_t hcclGetVersionString(char* pVersion, const unsigned len);

/* Returns the HCCL_VERSION_CODE of the HCCL library.
 * This integer is coded with the MAJOR, MINOR and PATCH level of the HCCL library.
 */
hcclResult_t hcclGetVersion_impl(int* version);

/* Generates an Id to be used in hcclCommInitRank. hcclGetUniqueId should be
 * called once and the Id should be distributed to all ranks in the
 * communicator before calling hcclCommInitRank. */
hcclResult_t hcclGetUniqueId_impl(hcclUniqueId* uniqueId);

/* Creates a new communicator (multi thread/process version).
 * rank must be between 0 and nranks-1 and unique within a communicator clique.
 * Each rank is associated to a Habana device, which has to be set before calling
 * hcclCommInitRank.
 * hcclCommInitRank implicitly synchronizes with other ranks, so it must be
 * called by different threads/processes or use hcclGroupStart/hcclGroupEnd. */
hcclResult_t hcclCommInitRank_impl(hcclComm_t* comm, int nranks, hcclUniqueId commId, int rank);

/* Creates a clique of communicators (single process version).
 * This is a convenience function to create a single-process communicator clique.
 * Returns an array of ndev newly initialized communicators in comm.
 * comm should be pre-allocated with size at least ndev*sizeof(hcclComm_t).
 * If devlist is NULL, the first ndev Habana devices are used.
 * Order of devlist defines user-order of processors within the communicator. */
hcclResult_t hcclCommInitAll_impl(hcclComm_t* comm, int ndev, const int* devlist);

/* Waits for all submitted work of communicator to be done.
 * By doing so, prepares communicator for destruction. */
hcclResult_t hcclCommFinalize_impl(hcclComm_t comm);

/* Frees resources associated with communicator object, but waits for any operations
 * that might still be running on the device. */
hcclResult_t hcclCommDestroy_impl(hcclComm_t comm);

/* Frees resources associated with communicator object and aborts any operations
 * that might still be running on the device. */
hcclResult_t hcclCommAbort_impl(hcclComm_t comm);

/* Returns a human-readable error message. */
const char* hcclGetErrorString_impl(hcclResult_t result);

/* returns details about the last error occurred in HCCL. nullptr if no details available */
const char* hcclGetLastErrorMessage_impl();

/* Checks whether the comm has encountered any asynchronous errors */
hcclResult_t hcclCommGetAsyncError_impl(hcclComm_t comm, hcclResult_t* asyncError);

/* returns details about the async comm error. nullptr if no details available */
const char* hcclCommGetAsyncErrorMessage_impl(hcclComm_t comm);

/* Gets the number of ranks in the communicator clique. */
hcclResult_t hcclCommCount_impl(hcclComm_t comm, int* count);

/* Returns the Habana device number associated with the communicator. */
hcclResult_t hcclCommSynDevice_impl(hcclComm_t comm, int* device);

/* Returns the user-ordered "rank" associated with the communicator. */
hcclResult_t hcclCommUserRank_impl(hcclComm_t comm, int* rank);

/* Returns FD for HBM memory region if it was registered for gaudi-direct. */
int hcclLookupDMABuff_impl(uint64_t addr, uint64_t size, int* fd);

/*
 * Collective communication operations
 *
 * Collective communication operations must be called separately for each
 * communicator in a communicator clique.
 *
 * They return when operations have been enqueued on the Habana stream.
 *
 * Since they may perform inter-CPU synchronization, each call has to be done
 * from a different thread or process, or need to use Group Semantics (see
 * below).
 */

/*
 * Reduce
 *
 * Reduces data arrays of length count in sendbuff into recvbuff using op
 * operation.
 * recvbuff may be NULL on all calls except for root device.
 * root is the rank (not the Habana device) where data will reside after the
 * operation is complete.
 *
 * In-place operation will happen if sendbuff == recvbuff.
 */
hcclResult_t hcclReduce_impl(const void*     sendbuff,
                             void*           recvbuff,
                             size_t          count,
                             hcclDataType_t  datatype,
                             hcclRedOp_t     reduceOp,
                             int             root,
                             hcclComm_t      comm,
                             synStreamHandle stream_handle);

/*
 * (deprecated) Broadcast (in-place)
 *
 * Copies count values from root to all other devices.
 * root is the rank (not the Habana device) where data resides before the
 * operation is started.
 *
 * This operation is implicitly in place.
 */
hcclResult_t hcclBcast_impl(void*           buff,
                            size_t          count,
                            hcclDataType_t  datatype,
                            int             root,
                            hcclComm_t      comm,
                            synStreamHandle stream_handle);

/*
 * Broadcast
 *
 * Copies count values from root to all other devices.
 * root is the rank (not the Habana device) where data resides before the
 * operation is started.
 *
 * In-place operation will happen if sendbuff == recvbuff.
 */
hcclResult_t hcclBroadcast_impl(const void*     sendbuff,
                                void*           recvbuff,
                                size_t          count,
                                hcclDataType_t  datatype,
                                int             root,
                                hcclComm_t      comm,
                                synStreamHandle stream_handle);

/*
 * All-Reduce
 *
 * Reduces data arrays of length count in sendbuff using op operation, and
 * leaves identical copies of result on each recvbuff.
 *
 * In-place operation will happen if sendbuff == recvbuff.
 */
hcclResult_t hcclAllReduce_impl(const void*     sendbuff,
                                void*           recvbuff,
                                size_t          count,
                                hcclDataType_t  datatype,
                                hcclRedOp_t     reduceOp,
                                hcclComm_t      comm,
                                synStreamHandle stream_handle);

/*
 * Reduce-Scatter
 *
 * Reduces data in sendbuff using op operation and leaves reduced result
 * scattered over the devices so that recvbuff on rank i will contain the i-th
 * block of the result.
 * Assumes sendcount is equal to nranks*recvcount, which means that sendbuff
 * should have a size of at least nranks*recvcount elements.
 *
 * In-place operations will happen if recvbuff == sendbuff + rank * recvcount.
 */
hcclResult_t hcclReduceScatter_impl(const void*     sendbuff,
                                    void*           recvbuff,
                                    size_t          recvcount,
                                    hcclDataType_t  datatype,
                                    hcclRedOp_t     reduceOp,
                                    hcclComm_t      comm,
                                    synStreamHandle stream_handle);

/*
 * All-Gather
 *
 * Each device gathers sendcount values from other HPUs into recvbuff,
 * receiving data from rank i at offset i*sendcount.
 * Assumes recvcount is equal to nranks*sendcount, which means that recvbuff
 * should have a size of at least nranks*sendcount elements.
 *
 * In-place operations will happen if sendbuff == recvbuff + rank * sendcount.
 */
hcclResult_t hcclAllGather_impl(const void*     sendbuff,
                                void*           recvbuff,
                                size_t          sendcount,
                                hcclDataType_t  datatype,
                                hcclComm_t      comm,
                                synStreamHandle stream_handle);

/*
 * AlltoAll
 *
 * preform send and receive operations from all ranks\peers to all ranks\peers.
 *
 *
 */
hcclResult_t hcclAlltoAll_impl(const void*     sendbuff,
                               void*           recvbuff,
                               size_t          count,
                               hcclDataType_t  datatype,
                               hcclComm_t      comm,
                               synStreamHandle stream_handle);

// /*
//  * Barrier
//  * Not implemented for Gen2
//  */
hcclResult_t hcclBarrier_impl(hcclComm_t comm, synStreamHandle stream_handle);

/*
 * Point to Point communications
 *
 * Point-to-point communication primitives are used to send and receive arbitrary data between any pair of communicator
 * ranks.
 */

/*
 * Send
 *
 * Sends data from \sendbuff to rank \peer. Rank \peer needs to call hcclRecv with the same \datatype and the same
 * \count from this rank. Blocking for HPU unless operation is part of group within hcclGroupStart() and hcclGroupEnd()
 * section.
 */
hcclResult_t hcclSend_impl(const void*     sendbuff,
                           size_t          count,
                           hcclDataType_t  datatype,
                           int             peer,
                           hcclComm_t      comm,
                           synStreamHandle stream);

/*
 * Receive
 *
 * Receives data from rank \peer to \recvbuff.
 * Rank \peer needs to call hcclSend with the same \datatype and the same \count to this rank.
 * Blocking for HPU unless operation is part of group within hcclGroupStart() and hcclGroupEnd() section.
 */
hcclResult_t
hcclRecv_impl(void* recvbuff, size_t count, hcclDataType_t datatype, int peer, hcclComm_t comm, synStreamHandle stream);

/*
 * Group semantics
 *
 * When managing multiple HPUs from a single thread, and since HCCL collective
 * calls may perform inter-CPU synchronization, we need to "group" calls for
 * different ranks/devices into a single call.
 *
 * Grouping HCCL calls as being part of the same collective operation is done
 * using hcclGroupStart and hcclGroupEnd. hcclGroupStart will enqueue all
 * collective calls until the hcclGroupEnd call, which will wait for all calls
 * to be complete. Note that for collective communication, hcclGroupEnd only
 * guarantees that the operations are enqueued on the streams, not that
 * the operation is effectively done.
 *
 * Both collective communication and hcclCommInitRank can be used in conjunction
 * of hcclGroupStart/hcclGroupEnd.
 */

/*
 * Group Start
 *
 * Start a group call. All subsequent calls to HCCL may not block due to
 * inter-CPU synchronization.
 */
hcclResult_t hcclGroupStart_impl();

/*
 * Group End
 *
 * End a group call. Wait for all calls since hcclGroupStart to complete
 * before returning.
 */
hcclResult_t hcclGroupEnd_impl();

/*
 * is ccb half full on the fastest micro stream
 */
bool hcclIsACcbHalfFull_impl(const unsigned archStreamIdx);

/* Associate device and context with Network layer */
hcclResult_t hcclDeviceInit_impl(void* device, void* context);
/*
 * Used for the profiler timer in HPT.
 * creates a marker event in the profiler traces. two markers can be used to measure a time window
 */
void hcclSetTraceMarker_impl(const synStreamHandle stream_handle, uint32_t val);

}  // namespace HclGen2
