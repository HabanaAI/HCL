#pragma once

#include <cstdint>  // for uint64_t, uint32_t
#include <vector>   // for vector
#include <memory>   // for unique_ptr

#include "hcl_api_types.h"                                      // for HCL_Comm
#include "synapse_api_types.h"                                  // for synStreamHandle
#include "synapse_common_types.h"                               // for synDeviceType
#include "hccl_types.h"                                         // for hcclRedOp_t, hcclResult_t
#include "platform/gen2_arch_common/api_aggregator.h"           // for ApiAggregatorGen2Arch
#include "platform/gen2_arch_common/hcl_device.h"               // for HclDeviceGen2Arch
#include "platform/gen2_arch_common/hcl_collective_routines.h"  // for HclCollectiveRoutinesGen2Arch
#include "hcl_api_entry.h"                                      // for ApiType, Recv
#include "hcl_dynamic_communicator.h"

class HclConfig;
class HclDeviceConfig;
class IHclCollectiveRoutines;

using hcl_device_t = HclDeviceGen2Arch*;

class hccl_device_t
{
    template<class T>
    class vector_t : public std::vector<T>
    {
    public:
        void clear()
        {
            for (T _elem : (*this))
            {
                delete _elem;
            }
            std::vector<T>::clear();
        }
        virtual ~vector_t() { clear(); }
    };

    class aggregators_t : public vector_t<ApiAggregatorGen2Arch*>
    {
    public:
        aggregators_t() { init(); }
        void init();
    };

    using collectives_t = vector_t<HclCollectiveRoutinesGen2Arch*>;

public:
    static hcclResult_t create(HclDeviceConfig& deviceConfig, const uint8_t apiId);
    static void         destroy();

    hccl_device_t() = default;
    virtual ~hccl_device_t() noexcept(false);

    virtual hcclResult_t init(uint8_t apiId);
    virtual void         initComm(const HCL_Comm commId);
    virtual hcclResult_t group(bool start);
    virtual hcclResult_t send_recv_call(int myRank, const SendRecvApiEntry& entry);
    virtual hcclResult_t collective_call(HclCollectiveParams& params);

    virtual hcl_device_t operator->() { return device_; }
    virtual              operator hcl_device_t() { return device_; }

    const collectives_t& collectives = collectives_;

    const bool initialized = false;

protected:
    hccl_device_t(HclDeviceGen2Arch* _device, synDeviceType _type) : initialized(true), device_(_device), type_(_type)
    {
    }
    virtual hcclResult_t init_device(uint8_t apiId) = 0;

    hcl_device_t device_ = nullptr;

    const synDeviceType type_ = synDeviceTypeInvalid;

    collectives_t collectives_;

    static thread_local aggregators_t aggregators_;
};

hccl_device_t& hccl_device();
