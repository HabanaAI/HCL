#include "platform/gen2_arch_common/hccl_device.h"

#include <cstring>  // for memcpy
#include <array>    // for array
#include <memory>   // for __shared_p...

#include "hccl_internal_defs.h"                           // for hcclHandle
#include "hccl_types.h"                                   // for hcclSuccess
#include "platform/gen2_arch_common/hcl_device_config.h"  // for HclDeviceConfig
#include "hcl_dynamic_communicator.h"                     // for HclDynamic...
#include "hcl_global_conf.h"                              // for GCFG_BOX_T...
#include "hcl_public_streams.h"                           // for getStreamID
#include "interfaces/hcl_remote_device.h"                 // for HclRemoteD...
#include "hcl_types.h"                                    // for RankInfo
#include "hcl_utils.h"                                    // for LOG_HCL_DEBUG
#include "infra/hcl_affinity_manager.h"                   // for initialize...
#include "infra/scal/gen2_arch_common/scal_manager.h"     // for Gen2ArchSc...
#include "interfaces/hcl_unique_sorted_vector.h"          // for UniqueSort...
#include "hcl_log_manager.h"                              // for LOG_TRACE, LOG_DEBUG, LOG_INFO

#include "hcl_collective_params.h"  // for HclCollectiveParams
#include "hcl_device_control_factory.h"

class uninitialized_device_t : public hccl_device_t
{
public:
    virtual hcclResult_t group(bool start) override
    {
        VERIFY(false, "device not initialized");
        return hcclInvalidUsage;
    }
    virtual hcclResult_t send_recv_call(int myRank, const SendRecvApiEntry& entry) override
    {
        VERIFY(false, "device not initialized");
    }
    virtual hcclResult_t collective_call(HclCollectiveParams& params) override
    {
        VERIFY(false, "device not initialized");
    }
    virtual hcl_device_t operator->() override
    {
        VERIFY(false, "device not initialized");
        return nullptr;
    }
    virtual hcclResult_t init_device(uint8_t apiId) override
    {
        VERIFY(false, "device not initialized");
        return hcclInvalidUsage;
    }
    virtual hcclResult_t init(uint8_t apiId) override
    {
        VERIFY(false, "device not initialized");
        return hcclInvalidUsage;
    }
    virtual uint32_t stream_id(void* streamHandle) override
    {
        VERIFY(false, "device not initialized");
        return 0;
    }

    virtual operator hcl_device_t() override
    {
        VERIFY(false, "device not initialized");
        return nullptr;
    }
} uninitialized_device;

hccl_device_t* g_device = &uninitialized_device;

thread_local hccl_device_t::aggregators_t hccl_device_t::aggregators_;

hccl_device_t& hccl_device()
{
    return (*g_device);
}

void hccl_device_t::aggregators_t::init()
{
    if (hccl_device().initialized && (size() == 0))
    {
        FOR_I(hccl_device()->getHal().getMaxStreams())
        {
            push_back(new ApiAggregatorGen2Arch(hccl_device().collectives[i]));
        }
    }
}

void hccl_device_t::initComm(const HCL_Comm commId)
{
    for (HclCollectiveRoutinesGen2Arch* collective : collectives_)
    {
        collective->onCommInit(commId);
    }
}

void hccl_device_t::destroy()
{
    if (hccl_device().initialized)
    {
        HclControlDeviceFactory::destroyDevice(g_device);
        g_device = &uninitialized_device;
    }
}

hcclResult_t hccl_device_t::create(HclDeviceConfig& deviceConfig, const uint8_t apiId)
{
    if (hccl_device().initialized)
    {
        LOG_WARN(HCL,
                 "HCL device was already initialized for device ({}). skipping initialization. "
                 "Make sure that each HCL device is handled by different process",
                 (*g_device)->getHwModuleId());
        return hcclSuccess;
    }
    // Pin 2 threads to 2 CPUs, and the rest can go wherever.
    initializeCpuPinning(/*priorityThreadsCount=*/2);

    LOG_INFO(HCL,
             "creating device. type = {} null-submission {}",
             deviceConfig.getDeviceTypeStr(),
             GCFG_HCL_NULL_SUBMIT.value());

    g_device = HclControlDeviceFactory::initDevice(deviceConfig);

    return g_device->init(apiId);
}

hcclResult_t hccl_device_t::init(uint8_t apiId)
{
    hcclResult_t rc = init_device(apiId);  // call device specific init (overriden)
    aggregators_.init();

    return rc;
}

hcclResult_t hccl_device_t::group(bool start)
{
    hcclResult_t rc = hcclSuccess;
    for (auto& agg : aggregators_)
    {
        if (start)
        {
            agg->addGroupStart();
        }
        else
        {
            if ((rc = agg->addGroupEnd()) != hcclSuccess) break;
        }
    }

    return rc;
}

hccl_device_t::~hccl_device_t() noexcept(false)
{
    if (!initialized) return;

    int active_comms = device_->getNumActiveComms();
    if (active_comms > 0)
    {
        LOG_HCL_ERR(HCL, "Device destroy is called but {} communicators still active", active_comms);
    }

    collectives_.clear();
}

hcclResult_t hccl_device_t::send_recv_call(int myRank, const SendRecvApiEntry& entry)
{
    return aggregators_[stream_id(entry.streamHandle)]->addSendRecvApiCall(myRank, entry);
}

static hcclResult_t selfRankMemcpy(const HclCollectiveParams& params)
{
    // in place, nothing to do, return
    if (params.m_sendBufferAddr == params.m_recvBufferAddr)
    {
        LOG_DEBUG(HCL, "single rank, inplace - return");
        return hcclSuccess;
    }
    // not in place, copy input->output buffer, return
    else
    {
        auto rank = params.m_dynamicComm.getMyRank();

        SendRecvApiEntry entry {ApiType::Send,
                                params.m_apiId,
                                params.m_streamHandle,
                                params.m_sendBufferAddr,
                                params.m_count,
                                params.m_dataType,
                                rank,
                                params.m_dynamicComm,
                                params.m_dynamicComm.m_remoteDevices[rank]->header.hwModuleID,
                                params.m_dynamicComm.isRankInsideScaleupGroup(rank)};

        // group start
        hcclResult_t res = hccl_device().group(true);
        if (res != hcclSuccess)
        {
            LOG_ERR(HCL, "group start failed");
            return res;
        }

        // send to self
        res = hccl_device().send_recv_call(rank, entry);
        if (res != hcclSuccess)
        {
            LOG_ERR(HCL, "send failed");
            return res;
        }

        entry.apiType = ApiType::Recv;
        entry.address = params.m_recvBufferAddr;

        res = hccl_device().send_recv_call(rank, entry);
        if (res != hcclSuccess)
        {
            LOG_ERR(HCL, "receive failed");
            return res;
        }

        // group end
        res = hccl_device().group(false);
        if (res != hcclSuccess)
        {
            LOG_ERR(HCL, "group end failed");
        }
        return res;
    }
}

hcclResult_t hccl_device_t::collective_call(HclCollectiveParams& params)
{
    if (params.m_collectiveOp == eHCLReduce || params.m_collectiveOp == eHCLAllReduce ||
        params.m_collectiveOp == eHCLBroadcast || params.m_collectiveOp == eHCLReduceScatter ||
        params.m_collectiveOp == eHCLAllGather || params.m_collectiveOp == eHCLAll2All)
    {
        // single rank communicator, not loopback
        if (params.m_dynamicComm.getCommSize() == 1 &&
            (HclConfigType)GCFG_BOX_TYPE_ID.value() != HclConfigType::LOOPBACK)
        {
            return selfRankMemcpy(params);
        }
    }

    return aggregators_[stream_id(params.m_streamHandle)]->addCollectiveApiCall(params);
}
