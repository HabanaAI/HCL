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

#pragma once

#include <cstdint>       // for uint64_t
#include <cstddef>       // for size_t
#include <map>           // for map
#include <memory>        // for shared_ptr, unique_ptr
#include <string>        // for string
#include "hccl_types.h"  // for hcclResult_t, hcclU...
#include "platform/gen2_arch_common/hccl_device.h"
#include "interfaces/hcl_idevice.h"                       // for IHclDevice
#include "synapse_api_types.h"                            // for synDeviceId
#include "platform/gen2_arch_common/hcl_device_config.h"  // for HclDeviceConfig
#include "coordinator_defs.h"                             // for spHcclCoordinatorClient

class IHclDevice;
struct hcclOpParams;
struct internal_unique_id_t;
class hccl_communicator;

using comms_map_t = std::map<hcclComm_t, std::shared_ptr<hccl_communicator>>;

class hccl_context
{
public:
    hccl_context()  = default;
    ~hccl_context() = default;

    hcclResult_t init_device(const uint8_t apiId, void* device = nullptr, void* context = nullptr);
    hcclResult_t destroy_device();

    hcclResult_t get_unique_id(hcclUniqueId* unique_id);
    hcclResult_t comm_init_rank(hcclComm_t* comm, unsigned int nranks, hcclUniqueId& comm_id, int rank);

    hcclResult_t comm_destroy(hcclComm_t unique_id);

    hccl_communicator* communicator(hcclComm_t comm_handle);

    uint8_t generateApiId();

    void        generateGlobalUniqueId(hcclUniqueId& unique_id);
    std::string unique_id_to_string(const hcclUniqueId& id);
    int         hccl_lookup_dma_buff_ctx();
    void        dfaLog(hl_logger::LoggerSPtr logger);
    void        dfaLogHnicSummary(hl_logger::LoggerSPtr& logger);

    bool first_comm_init_ = false;

    const comms_map_t& comms() const { return hccl_communicators_; };

private:
    bool first_coordinator_launched_ = false;

    const internal_unique_id_t* get_internal_id(const hcclUniqueId& unique_id) const;

    // relationship between communicator and coordinator is defined by the unique ID
    // so both communicator and coordinator resources can be removed on comm_destroy

    // coordinators list mapped by unique ID
    // a coordinator is added only on the coordinator rank
    std::map<std::string, HcclCoordinatorUPtr> coordinators_;

    // communicators list mapped by comm handle
    comms_map_t hccl_communicators_;

    // The following is an indication if this device was acquired by synapse successfully and it is then sets to true.
    // When the device is destroyed it is set to false
    bool device_acquired_ = false;

    std::unique_ptr<HclDeviceConfig> device_config_ = nullptr;
};

extern hccl_context hccl_ctx;
