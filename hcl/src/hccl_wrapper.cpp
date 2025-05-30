#include "hccl_wrapper.h"
#include "hccl.h"

#include "hccl_api_inc.h"       // for HCCL_TRY
#include "hccl_helpers.h"       // for to_string, to_hccl_...
#include "hccl_context.h"       // for hccl_context, g_hcc...
#include "hccl_communicator.h"  // for hccl_communicator
#include "network_utils.h"      // for get_global_comm_id

hcclResult_t hcclGetVersion_Wrapper(int* version)
{
    HCCL_TRY
    RETURN_ON_NULL_ARG(version);

    *version = HCCL_VERSION_CODE;
    LOG_DEBUG(HCL_API, "HCCL Version is: {}", *version);
    HCCL_API_EXIT(hcclSuccess)
}

hcclResult_t hcclDeviceInit_Wrapper([[maybe_unused]] void* device, [[maybe_unused]] void* context)
{
    HCCL_TRY
    hcclResult_t status = hccl_ctx.init_device(hccl_ctx.generateApiId(), device, context);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclGetUniqueId_Wrapper(hcclUniqueId* uniqueId)
{
    HCCL_TRY
    hcclResult_t status = hccl_ctx.get_unique_id(uniqueId);
    HCCL_API_EXIT(status)
}

bool use_global_comm_id()
{
    return (!hccl_ctx.first_comm_init_ && !get_global_comm_id().empty());
}

hcclResult_t hcclCommInitRank_Wrapper(hcclComm_t* comm, int nranks, hcclUniqueId& commId, int rank)
{
    HCCL_CHECK_STOP_API();

    HCCL_TRY

    if (use_global_comm_id())
    {
        hccl_ctx.generateGlobalUniqueId(commId);
    }

    std::string comm_id_str = hccl_ctx.unique_id_to_string(commId);
    HCL_API_LOG_ENTRY("(nranks={}, commId={}, rank={})", nranks, comm_id_str, rank);
    hcclResult_t res = hccl_ctx.comm_init_rank(comm, nranks, commId, rank);
    if (res == hcclSuccess)
    {
        HCL_API_LOG_ENTRY("(comm={})", *comm);
    }
    else
    {
        LOG_ERR(HCL_API, "hcclCommInitRank_Wrapper failed({})", res);
    }

    HCCL_API_EXIT(res)
}

hcclResult_t hcclCommInitAll_Wrapper([[maybe_unused]] hcclComm_t* comm,
                                     [[maybe_unused]] int         ndev,
                                     [[maybe_unused]] const int*  devlist)
{
    HCCL_TRY
    LOG_ERR(HCL_API, "hcclCommInitAll not implemented!");
    setLastErrorMessage("hcclCommInitAll not implemented!");
    hcclResult_t status = hcclInvalidUsage;
    HCCL_API_EXIT(status)
}

hcclResult_t hcclCommFinalize_Wrapper(hcclComm_t comm)
{
    HCCL_TRY
    auto* hcclComm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hcclComm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hcclComm);

    hcclComm->finalize();
    HCCL_API_EXIT(hcclResult_t::hcclSuccess)
}

hcclResult_t hcclCommDestroy_Wrapper(hcclComm_t comm)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hccl_comm);

    hcclResult_t status = hccl_ctx.comm_destroy(comm);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclCommAbort_Wrapper(hcclComm_t comm)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hccl_comm);

    hcclResult_t status = hcclCommDestroy(comm);
    HCCL_API_EXIT(status)
}

const char* hcclGetErrorString_Wrapper(hcclResult_t result)
{
    return get_error_string(result);
}

const unsigned           lastErrorMessageSize                     = 1024;
static thread_local char g_lastErrorMessage[lastErrorMessageSize] = {0};

void setLastErrorMessage(const std::string& message)
{
    strncpy(g_lastErrorMessage, message.c_str(), lastErrorMessageSize);
    g_lastErrorMessage[lastErrorMessageSize - 1] = 0;
}

void resetLastErrorMessage()
{
    g_lastErrorMessage[0] = 0;
}

const char* hcclGetLastErrorMessage_Wrapper()
{
    return g_lastErrorMessage[0] == 0 ? nullptr : g_lastErrorMessage;
}

hcclResult_t hcclCommGetAsyncError_Wrapper(hcclComm_t comm, hcclResult_t* asyncError)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hccl_comm);

    hcclResult_t status = hccl_comm->get_async_error(asyncError);
    HCCL_API_EXIT(status)
}

const char* hcclCommGetAsyncErrorMessage_Wrapper(hcclComm_t comm)
{
    waitForDfaFinish();
    if (comm == nullptr)
    {
        std::string globalAsyncStatusMessage = getGlobalAsyncErrorStatusMessage();
        if (!globalAsyncStatusMessage.empty())
        {
            const unsigned    bufferSize = 1024;
            thread_local char globalStatusMessageTL[bufferSize];
            strncpy(globalStatusMessageTL, globalAsyncStatusMessage.c_str(), bufferSize);
            globalStatusMessageTL[bufferSize - 1] = '\0';
            return globalStatusMessageTL;
        }
        else
        {
            return nullptr;
        }
    }

    try
    {
        auto* hccl_comm = hccl_ctx.communicator(comm);
        if (hccl_comm == nullptr)
        {
            setLastErrorMessage("Invalid HCCL communicator handle.");
            return nullptr;
        }

        return hccl_comm->get_async_error_message();
    }
    catch (hcl::VerifyException& e)
    {
        waitForDfaFinish();
        auto status = getGlobalDfaStatus();
        LOG_CRITICAL(HCL, "{} returned {} with exception: {}", HLLOG_FUNC, status, e.what());
        setLastErrorMessage("exception occurred: " + std::string(e.what()));
        return nullptr;
    }
    catch (...)
    {
        LOG_CRITICAL(HCL, "{} returned with unknown exception", HLLOG_FUNC);
        setLastErrorMessage("unknown exception");
        return nullptr;
    }
}

hcclResult_t hcclCommCount_Wrapper(hcclComm_t comm, int* count)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    hcclResult_t status = hccl_comm->comm_count(count);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclCommSynDevice_Wrapper(hcclComm_t comm, [[maybe_unused]] int* device)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    HCCL_API_EXIT(hcclSuccess)
}

hcclResult_t hcclCommUserRank_Wrapper(hcclComm_t comm, int* rank)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    hcclResult_t status = hccl_comm->comm_user_rank(rank);
    HCCL_API_EXIT(status)
}

int hcclLookupDMABuff_Wrapper([[maybe_unused]] uint64_t addr, [[maybe_unused]] uint64_t size, int* fd)
{
    HCCL_TRY
    RETURN_ON_INVALID_FD(fd);
    const int retval = hccl_ctx.hccl_lookup_dma_buff_ctx();
    if (retval < 0) return retval;
    *fd = retval;
    HCCL_API_EXIT(0)
}

hcclResult_t hcclReduceScatter_Wrapper(const void*    sendbuff,
                                       void*          recvbuff,
                                       size_t         recvcount,
                                       hcclDataType_t datatype,
                                       hcclRedOp_t    reduceOp,
                                       hcclComm_t     comm,
                                       void*          stream_handle)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    // Data validation
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    RETURN_ON_INVALID_ADDR(sendbuff);
    RETURN_ON_INVALID_ADDR(recvbuff);
    RETURN_ON_INVALID_DATA_TYPE(datatype);
    RETURN_ON_INVALID_REDUCTION_OP(reduceOp);
    RETURN_ON_INVALID_STREAM(stream_handle);

    uint8_t apiId = hccl_ctx.generateApiId();

    // report collective log
    HCL_COLLECTIVE_LOG(eHCLReduceScatter, recvcount, datatype, reduceOp, -1, -1);

    hcclResult_t status =
        hccl_comm
            ->reduce_scatter(sendbuff, recvbuff, recvcount, datatype, reduceOp, stream_handle, eHCCLAPICall, apiId);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclReduce_Wrapper(const void*    sendbuff,
                                void*          recvbuff,
                                size_t         count,
                                hcclDataType_t datatype,
                                hcclRedOp_t    reduceOp,
                                int            root,
                                hcclComm_t     comm,
                                void*          stream_handle)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    RETURN_ON_INVALID_ADDR(sendbuff);
    if (hccl_comm->user_rank() == root)  // recvbuff may be NULL on all calls except for root device
    {
        RETURN_ON_INVALID_ADDR(recvbuff);
    }
    RETURN_ON_INVALID_DATA_TYPE(datatype);
    RETURN_ON_INVALID_REDUCTION_OP(reduceOp);
    RETURN_ON_INVALID_RANK(root, hccl_comm->getCommSize());
    RETURN_ON_INVALID_STREAM(stream_handle);

    uint8_t apiId = hccl_ctx.generateApiId();

    // report collective log
    HCL_COLLECTIVE_LOG(eHCLReduce, count, datatype, reduceOp, -1, root);

    hcclResult_t status =
        hccl_comm->reduce(sendbuff, recvbuff, count, datatype, reduceOp, root, stream_handle, eHCCLAPICall, apiId);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclAllReduce_Wrapper(const void*    sendbuff,
                                   void*          recvbuff,
                                   size_t         count,
                                   hcclDataType_t datatype,
                                   hcclRedOp_t    reduceOp,
                                   hcclComm_t     comm,
                                   void*          stream_handle)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    RETURN_ON_INVALID_ADDR(sendbuff);
    RETURN_ON_INVALID_ADDR(recvbuff);
    RETURN_ON_INVALID_DATA_TYPE(datatype);
    RETURN_ON_INVALID_REDUCTION_OP(reduceOp);
    RETURN_ON_INVALID_STREAM(stream_handle);

    uint8_t apiId = hccl_ctx.generateApiId();

    // report collective log
    HCL_COLLECTIVE_LOG(eHCLAllReduce, count, datatype, reduceOp, -1, -1);

    hcclResult_t status =
        hccl_comm->allreduce(sendbuff, recvbuff, count, datatype, reduceOp, stream_handle, eHCCLAPICall, apiId);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclBroadcast_Wrapper(const void*    sendbuff,
                                   void*          recvbuff,
                                   size_t         count,
                                   hcclDataType_t datatype,
                                   int            root,
                                   hcclComm_t     comm,
                                   void*          stream_handle)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    RETURN_ON_INVALID_ADDR(sendbuff);
    RETURN_ON_INVALID_ADDR(recvbuff);
    RETURN_ON_INVALID_DATA_TYPE(datatype);
    RETURN_ON_INVALID_RANK(root, hccl_comm->getCommSize());
    RETURN_ON_INVALID_STREAM(stream_handle);

    uint8_t apiId = hccl_ctx.generateApiId();

    // report collective log
    HCL_COLLECTIVE_LOG(eHCLBroadcast, count, datatype, hcclOpNone, -1, root);

    hcclResult_t status =
        hccl_comm->broadcast(sendbuff, recvbuff, count, datatype, root, stream_handle, eHCCLAPICall, apiId);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclAllGather_Wrapper(const void*    sendbuff,
                                   void*          recvbuff,
                                   size_t         sendcount,
                                   hcclDataType_t datatype,
                                   hcclComm_t     comm,
                                   void*          stream_handle)
{
    HCCL_TRY

    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    RETURN_ON_INVALID_ADDR(sendbuff);
    RETURN_ON_INVALID_ADDR(recvbuff);
    RETURN_ON_INVALID_DATA_TYPE(datatype);
    RETURN_ON_INVALID_STREAM(stream_handle);

    uint8_t apiId = hccl_ctx.generateApiId();

    // report collective log
    HCL_COLLECTIVE_LOG(eHCLAllGather, sendcount, datatype, hcclOpNone, -1, -1);

    uint64_t sendSizePerRank = sendcount * hccl_data_type_elem_size(datatype);

    // overlapping buffers
    if (!((uint64_t)sendbuff + sendSizePerRank <= (uint64_t)recvbuff ||
          (uint64_t)sendbuff >= (uint64_t)recvbuff + sendSizePerRank * hccl_comm->getCommSize()))
    {
        if ((uint64_t)sendbuff != (uint64_t)recvbuff + sendSizePerRank * hccl_comm->user_rank())
        {
            LOG_ERR(HCL_API, "sendbuff and recvbuff are overlapping but not in place");
            return hcclInvalidArgument;
        }
    }

    hcclResult_t status =
        hccl_comm->allgather(sendbuff, recvbuff, sendcount, datatype, stream_handle, eHCCLAPICall, apiId);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclAlltoAll_Wrapper(const void*    sendbuff,
                                  void*          recvbuff,
                                  size_t         count,
                                  hcclDataType_t datatype,
                                  hcclComm_t     comm,
                                  void*          stream_handle)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    RETURN_ON_INVALID_ADDR(sendbuff);
    RETURN_ON_INVALID_ADDR(recvbuff);
    RETURN_ON_INVALID_DATA_TYPE(datatype);
    RETURN_ON_INVALID_STREAM(stream_handle);

    uint8_t apiId = hccl_ctx.generateApiId();

    size_t   n_ranks   = hccl_comm->getCommSize();
    uint64_t remainder = count % n_ranks;
    if (remainder != 0)
    {
        LOG_ERR(HCL_API,
                "hcclAlltoAll count should equally divide by number of ranks while count is {} and number "
                "of ranks is {}",
                count,
                n_ranks);
        return hcclInvalidArgument;
    }

    // report collective log
    HCL_COLLECTIVE_LOG(eHCLAll2All, count, datatype, hcclOpNone, -1, -1);

    hcclResult_t status = hccl_comm->alltoall(sendbuff, recvbuff, count, datatype, stream_handle, eHCCLAPICall, apiId);

    HCCL_API_EXIT(status)
}

hcclResult_t hcclSend_Wrapper(const void*    sendbuff,
                              size_t         count,
                              hcclDataType_t datatype,
                              int            peer,
                              hcclComm_t     comm,
                              void*          stream_handle)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    RETURN_ON_INVALID_ADDR(sendbuff);
    RETURN_ON_INVALID_DATA_TYPE(datatype);
    RETURN_ON_INVALID_STREAM(stream_handle);
    RETURN_ON_RANK_CHECK(peer, hccl_comm);

    hccl_comm->getDynamicComm()->incApiPreGroupEndSendCounter(peer);

    // report collective log
    HCL_COLLECTIVE_LOG(eHCLNoCollective, count, datatype, hcclOpNone, peer, 0);

    hcclResult_t status = hccl_comm->hccl_send(sendbuff, count, datatype, peer, stream_handle, HCL_DEFAULT_API_ID);

    HCCL_API_EXIT(status)
}

hcclResult_t
hcclRecv_Wrapper(void* recvbuff, size_t count, hcclDataType_t datatype, int peer, hcclComm_t comm, void* stream_handle)
{
    HCCL_TRY
    auto* hccl_comm = hccl_ctx.communicator(comm);
    RETURN_ON_INVALID_HCCL_COMM(hccl_comm);
    RETURN_ON_INVALID_ADDR(recvbuff);
    RETURN_ON_INVALID_DATA_TYPE(datatype);
    RETURN_ON_INVALID_STREAM(stream_handle);
    RETURN_ON_RANK_CHECK(peer, hccl_comm);

    hccl_comm->getDynamicComm()->incApiPreGroupEndRecvCounter(peer);

    // report collective log
    HCL_COLLECTIVE_LOG(eHCLNoCollective, count, datatype, hcclOpNone, peer, -1);

    // Receive using HCL will be aggregated on HCL level
    hcclResult_t status = hccl_comm->hccl_receive(recvbuff, count, datatype, peer, stream_handle, HCL_DEFAULT_API_ID);

    HCCL_API_EXIT(status)
}

hcclResult_t hcclGroupStart_Wrapper()
{
    HCCL_TRY
    hcclResult_t status = hccl_device().group(true);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclGroupEnd_Wrapper()
{
    HCCL_TRY
    hcclResult_t status = hccl_device().group(false);
    HCCL_API_EXIT(status)
}

hcclResult_t hcclInitDevice_Wrapper([[maybe_unused]] const uint32_t deviceId)
{
    HCCL_TRY
    hcclResult_t status = hccl_ctx.init_device(hccl_ctx.generateApiId());
    HCCL_API_EXIT(status)
}

hcclResult_t hcclDestroyDevice_Wrapper([[maybe_unused]] const uint32_t deviceId)
{
    HCCL_TRY
    hcclResult_t status = hccl_ctx.destroy_device();
    HCCL_API_EXIT(status)
}
