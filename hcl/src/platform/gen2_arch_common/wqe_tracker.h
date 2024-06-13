#pragma once
#include "hcl_api_types.h"
#include "hcl_dynamic_communicator.h"

enum class QpType
{
    ScaleUpAllGather      = 0,
    ScaleUpReduceScatter  = 1,
    ScaleOutAllGather     = 2,
    ScaleOutReduceScatter = 3,
    QPTypeSize            = 4
};

struct WqeWraparoundBits
{
    bool notify_rndv_ack;
    bool wait_for_rndv_acks;
};

typedef std::array<std::vector<std::vector<uint64_t>>, (unsigned)QpType::QPTypeSize> WqePerConnection;
typedef std::array<std::vector<std::vector<WqeWraparoundBits>>, (unsigned)QpType::QPTypeSize> WqeWraparoundBitsPerQp;

class WqeTracker
{
public:
    WqeTracker() = default;
    virtual ~WqeTracker() = default;

    WqeTracker(WqeTracker&&)      = default;  // ALLOW move ctor
    WqeTracker(const WqeTracker&) = delete;
    WqeTracker& operator=(WqeTracker&&) = delete;
    WqeTracker& operator=(const WqeTracker&) = delete;

    virtual void              incWqe(const HCL_Comm commId, const unsigned rank, const QpType qpType) {}
    virtual WqeWraparoundBits getWqeWraparoundBits(HCL_Comm commId, unsigned rank, QpType qpType)
    {
        return {false, false};
    }
    void setRecvWqeEntriesNum(unsigned recvWqeEntriesNum) {m_recvWqeEntriesNum = recvWqeEntriesNum;}

protected:
    unsigned m_recvWqeEntriesNum = 0;
};