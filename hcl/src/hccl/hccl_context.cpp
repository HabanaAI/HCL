/******************************************************************************
 * Copyright (C) 2021 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

#include "hccl_context.h"

#include <arpa/inet.h>   // for inet_pton
#include <netinet/in.h>  // for sockaddr_in, htons
#include <cstring>       // for memcpy
#include <sys/socket.h>  // for AF_INET, sockaddr
#include <utility>       // for move

#include "hccl_communicator.h"       // for hccl_communicator
#include "hccl_helpers.h"            // for RETURN_ON_ERROR, RETURN_ON_H...
#include "hccl_internal_defs.h"      // for internal_unique_id_t...
#include "hccl_types.h"              // for hcclResult_t,SYN_VALID_DEVICE_ID
#include "hcl_global_conf.h"         // for GCFG_HCCL_OVER_OFI, GCFG_HCC...
#include "hcl_utils.h"               // for LOG_HCL_ERR, LOG_HCL_INFO
#include "interfaces/hcl_idevice.h"  // for IHclDevice
#include "network_utils.h"           // for address_to_string, get_globa...
#include "ofi_plugin.h"              // for OfiPlugin
#include "hcl_log_manager.h"         // for LOG_ERR, LOG_DEBUG, LOG_INFO
#include "infra/hcl_sockaddr.h"
#include "hcl_device_config_factory.h"  // for HclDeviceConfigFactory
#include "hcl_dynamic_communicator.h"   // for HclDynamicCommunicator
#include "platform/gen2_arch_common/server_def.h"
#include "coordinator/qp_migration.h"  // for NicState
#include "ibverbs/hcl_ibverbs.h"       // for g_ibv.has_ib_device()
#include "fault_tolerance_inc.h"       // for HLFT.* macros

void hccl_context::generateGlobalUniqueId(hcclUniqueId& unique_id)
{
    sockaddr_t addr(get_global_comm_ip().c_str(), get_global_comm_port());

    internal_unique_id_t globalUniqueId;

    globalUniqueId.address = addr;
    globalUniqueId.length  = sizeof(globalUniqueId.address);
    globalUniqueId.id      = CORD_ID_GLOBAL_COMM;

    memcpy(unique_id.internal, (uint8_t*)&globalUniqueId, sizeof(globalUniqueId));
    unique_id.length = sizeof(internal_unique_id_t);

    return;
}

// This function is used to init a comm.
// It is also called when doing re-init to a comm. In this case, the caller should give the previous handle
// so the re-init comm has the same handle (and the init is transparent to the user).
hcclResult_t hccl_context::comm_init_rank(hcclComm_t* comm_handle, unsigned int nranks, hcclUniqueId& comm_id, int rank)
{
    // This function must be called after the device was already initialized. For that to happen
    // the environment variable 'INIT_HCCL_ON_ACQUIRE' must be set to true, and for all HCL uses
    // this must be the case. For other users of the synapse library there might be a need to start
    // synapse without starting HCL, the flag gives them that option. But then they must not use
    // this function, therefore there is a check if the device does not exist already it will fail
    VERIFY(
        hccl_device().initialized,
        "The device was not initialized, please ensure the environment variable INIT_HCCL_ON_ACQUIRE is set to true");

    // log process memory
    LOG_HCL_INFO(HCL, "Start - Process memory size {}GB", getProcMemConsInGB());

    LOG_HCL_DEBUG(HCL,
                  "nranks={}, rank={}, getDevice()->getNumActiveComms()={}",
                  nranks,
                  rank,
                  hccl_device()->getNumActiveComms());

    RETURN_ON_NULL_ARG(comm_handle);

    // For single device case, only nranks==1 makes sense
    if (!g_ibv.has_ib_device() && (nranks > 1))
    {
        LOG_HCL_ERR(HCL, "No ibv device and number of ranks > 1");
        return hcclInvalidArgument;
    }

    if (nranks < 1 || nranks > GCFG_HCL_MAX_RANKS.value() || nranks > MAX_SUPPORTED_RANKS)
    {
        LOG_HCL_ERR(
            HCL,
            "Number of communicator ranks({}) should be in range [1 - {}]. (Note: HCL_MAX_RANKS can't exceed 8192.)",
            nranks,
            GCFG_HCL_MAX_RANKS.value() > MAX_SUPPORTED_RANKS ? MAX_SUPPORTED_RANKS : GCFG_HCL_MAX_RANKS.value());
        return hcclInvalidArgument;
    }

    const internal_unique_id_t*        internal_id = get_internal_id(comm_id);
    std::shared_ptr<hccl_communicator> spHcclComm(new hccl_communicator(rank, nranks));
    if (nullptr == spHcclComm)
    {
        LOG_HCL_ERR(HCL, "Creation of hccl communicator failed.");
        return hcclInternalError;
    }

    if (spHcclComm->initialize(internal_id) != hcclSuccess)
    {
        LOG_HCL_ERR(HCL, "Initialization of hccl communicator failed.");
        return hcclInternalError;
    }

    // for re-init case, use previous key
    *comm_handle = spHcclComm.get();

    // create a map entry as a pair of hcclComm key and hclComm key
    hccl_communicators_[*comm_handle] = spHcclComm;
    if (!first_comm_init_)
    {
        first_comm_init_            = true;
        first_coordinator_launched_ = true;
    }

    // log process memory
    LOG_HCL_INFO(HCL, "End - Process memory size {}GB", getProcMemConsInGB());

    // This log line should NOT change without a corresponding change in hclrec, as changes here might break that.
    LOG_HCL_INFO(HCL_API,
                 "Rank({}/{}) Created Communicator (hccl({})), on coordinator: {} commId {} hcclComm {}",
                 rank,
                 nranks,
                 *comm_handle,
                 spHcclComm->getCommUniqueId(),
                 (HCL_Comm)(*spHcclComm->getDynamicComm()),
                 spHcclComm.get());

    return hcclSuccess;
}

hccl_communicator* hccl_context::communicator(hcclComm_t comm_handle)
{
    auto it = hccl_communicators_.find(comm_handle);
    if (hccl_communicators_.end() == it)
    {
        LOG_HCL_ERR(HCL, "did not find matching HCL_Comm handle for hccl_communicator key {}", comm_handle);
        return nullptr;
    }
    return it->second.get();
}

hcclResult_t hccl_context::get_unique_id(hcclUniqueId* unique_id)
{
    RETURN_ON_NULL_ARG(unique_id);

    bool use_hccl_comm_id_env_var = !first_coordinator_launched_ && !get_global_comm_id().empty();

    // create coordinator and run it
    auto coordinator = IHcclCoordinator::create(use_hccl_comm_id_env_var);
    if (nullptr == coordinator)
    {
        LOG_HCL_ERR(HCL, "Failed to create coordinator.");
        return hcclSocketError;
    }

    if (use_hccl_comm_id_env_var)
    {
        first_coordinator_launched_ = true;
    }

    // generate the coordinator unique ID
    coordinator->get_unique_id(*unique_id);

    // store coordinator mapped by uniqueId
    std::string uniqueIdStr    = unique_id_to_string(*unique_id);
    coordinators_[uniqueIdStr] = std::move(coordinator);

    // force log info of the coordinator unique ID
    LOG_INFO_F(HCL_COORD, "Created Coordinator, Unique ID is: {}", uniqueIdStr);

    return hcclSuccess;
}

const internal_unique_id_t* hccl_context::get_internal_id(const hcclUniqueId& unique_id) const
{
    VERIFY((sizeof(internal_unique_id_t) == unique_id.length) || GCFG_HCL_NULL_SUBMIT.value(),
           "Invalid unique_id length={}. Please make sure that HCCL_COMM_ID env var is set",
           unique_id.length);
    return reinterpret_cast<const internal_unique_id_t*>(unique_id.internal);
}

hcclResult_t hccl_context::comm_destroy(hcclComm_t comm_handle)
{
    auto* hcclComm = communicator(comm_handle);
    if (hcclComm == nullptr)
    {
        return hcclInvalidArgument;
    }

    // get the communicator uniqueId, required to clean resources
    std::string id = hcclComm->getCommUniqueId();

    std::vector<std::unique_ptr<std::lock_guard<std::mutex>>> guards;
    for (size_t i = 0; i < hcclComm->getDynamicComm()->m_streamLatestLongSo.size(); i++)
    {
        // Use make_unique to create lock_guard objects
        // this is needed because std::vector requires the objects it stores to be copyable or moveable, and
        // std::lock_guard is neither
        guards.emplace_back(
            std::make_unique<std::lock_guard<std::mutex>>(hccl_device()->m_deviceController.getStreamLock(i)));
    }

    // first destroy communicator
    hcclComm->finalize(false);

    if (!hcclComm->destroy())
    {
        LOG_HCL_ERR(HCL, "Destruction of hccl communicator failed.");
        return hcclInternalError;
    }

    // clean mapped resources and handles
    // check if this is comm coordinator
    auto it = coordinators_.find(id);
    if (it != coordinators_.end())
    {
        // remove coordinator from list
        LOG_HCL_DEBUG(HCL, "Removing coordinator, unique ID({})", id);
        coordinators_.erase(id);
    }

    // remove communicator from list
    LOG_HCL_DEBUG(HCL, "Removing comm handle {} ptr {}, unique ID({})", comm_handle, hcclComm, id);
    hccl_communicators_.erase(comm_handle);
    hcclComm = nullptr;
    return hcclSuccess;
}

std::string hccl_context::unique_id_to_string(const hcclUniqueId& id)
{
    const internal_unique_id_t* internal_id = get_internal_id(id);
    return sockaddr_str_t(internal_id->address);
}

hcclResult_t hccl_context::init_device(const uint8_t apiId, void* device, void* context)
{
    LOG_HCL_DEBUG(HCL, "Started, m_deviceAcquired={}, apiId={}", device_acquired_, apiId);
    if (device_acquired_)
    {
        LOG_HCL_DEBUG(HCL,
                      "HCL device was already initialized. skipping initialization. "
                      "Make sure that each HCCL device is handled by different process");
        return hcclSuccess;
    }

    hclPrintVersionToLog();

    device_config_ = HclDeviceConfigFactory::createDeviceConfig(device, context);
    device_config_->init();

    hccl_device_t::create(*device_config_, apiId);

    device_acquired_ = true;

    return hcclSuccess;
}

hcclResult_t hccl_context::destroy_device()
{
    LOG_HCL_DEBUG(HCL, "Started, m_deviceAcquired={}", device_acquired_);
    if (!device_acquired_)
    {
        LOG_HCL_DEBUG(HCL,
                      "HCL device was not initialized for device. skipping destruction. "
                      "Make sure that each HCCL device is handled by different process");
        return hcclSuccess;
    }

    hccl_device().destroy();

    device_config_->destroy();

    device_acquired_ = false;
    return hcclSuccess;
}

int hccl_context::hccl_lookup_dma_buff_ctx()
{
    return hccl_device()->getOfiComponent()->getDmabufFd();
}

void hccl_context::dfaLog(hl_logger::LoggerSPtr logger)
{
    if (GCFG_HCCL_OVER_OFI.value())
    {
        dfaLogHnicSummary(logger);
    }

    HLLOG_UNTYPED(logger,
                  HLLOG_LEVEL_INFO,
                  "============================ HCCL communicators summary "
                  "========================================================");

    for (const auto& comm : hccl_communicators_)
    {
        hccl_communicator* hcclCommunicator   = comm.second.get();
        int                rank               = hcclCommunicator->user_rank();
        size_t             commSize           = hcclCommunicator->getCommSize();
        uint64_t           collectiveCallsCtr = hcclCommunicator->getCollectiveCtr();
        HCL_Comm           commId             = (HCL_Comm)(*hcclCommunicator->getDynamicComm());

        HLLOG_UNTYPED(logger,
                      HLLOG_LEVEL_INFO,
                      "hcclComm {:x} comm {:4} rank {:4} commSize {:4} uniqId {:20} collective#=0x{}",
                      TO64(hcclCommunicator),
                      commId,
                      rank,
                      commSize,
                      hcclCommunicator->getCommUniqueId(),
                      collectiveCallsCtr);
    }
}

void hccl_context::dfaLogHnicSummary(hl_logger::LoggerSPtr& logger)
{
    HLLOG_UNTYPED(
        logger,
        HLLOG_LEVEL_INFO,
        "============================ HCCL HNIC summary ========================================================");
    struct fi_info* info = nullptr;
    try
    {
        if (hccl_device() == nullptr)
        {
            // Handle the case where hccl_device() returns a null pointer
            HLLOG_UNTYPED(logger, HLLOG_LEVEL_ERROR, "Could not fetch hccl_device");
            return;
        }

        const auto ofiHandle = hccl_device()->getOfiHandle();
        if (ofiHandle == nullptr)
        {
            // Handle the case where getOfiHandle() returns a null pointer
            HLLOG_UNTYPED(logger, HLLOG_LEVEL_ERROR, "Could not fetch OfiHandle");
            return;
        }

        info = ofiHandle->get_nic_info(hccl_device()->getOfiDeviceId());
        if (info == nullptr)
        {
            // Handle the case where get_nic_info() returns a null pointer
            HLLOG_UNTYPED(logger, HLLOG_LEVEL_ERROR, "Could not fetch fi_info object");
            return;
        }
    }
    catch (const std::exception& e)
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_ERROR, "Could not fetch fi_info object: {}", e.what());
        return;
    }
    if (info != nullptr)
    {
        HLLOG_UNTYPED(logger,
                      HLLOG_LEVEL_INFO,
                      "Gaudi-direct is {}, HNIC domain_name={}, prov_name={}, active_mtu={}",
                      hccl_device()->getScaleOutProvider()->isGaudiDirect() ? "enabled" : "disabled",
                      info->domain_attr->name,
                      info->fabric_attr->prov_name,
                      info->nic->link_attr->mtu);
    }
}

uint8_t hccl_context::generateApiId()
{
    uint8_t apiId = HCL_DEFAULT_API_ID;
    if (synGenerateApiId(apiId) != synSuccess)
    {
        LOG_WARN(HCL, "synGenerateApiId failed, using default value {}", HCL_DEFAULT_API_ID);
    }
    return apiId;
}
